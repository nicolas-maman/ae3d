"""An agent channel inside a running Blender.

    blender --python tools/blender/ae3d_agent_server.py -- --port 7800
    blender --background <file.blend> --python tools/blender/ae3d_agent_server.py -- --port 7800 --serve 120

Speaks the protocol ae3d's engine speaks: one JSON object per line, every
answer carrying back the id of the request it answers, `help` returning the op
table as data. An agent that can drive the engine can drive Blender with the
same client and the same habits.

    {"id": 1, "op": "scene.objects"}
    {"id": 1, "ok": true, "result": {"count": 2, "objects": [...]}}

Requests are served on Blender's own thread. bpy is not thread-safe, and a
socket thread touching it crashes Blender rather than returning an error, so
the reader thread only queues whole lines and a timer on the main thread drains
them. In background mode there is no timer, so --serve pumps the queue directly
for a bounded number of seconds.
"""

import bpy
import argparse
import json
import os
import queue
import socket
import sys
import threading
import time

PROTOCOL = "ae3d-blender/1"

_requests = queue.Queue()
_client = None
_listener = None
_running = False


def op_help(_request):
    return {
        "protocol": PROTOCOL,
        "ops": [
            {"op": "help", "args": "", "summary": "This table."},
            {"op": "ping", "args": "", "summary": "Liveness."},
            {"op": "file.info", "args": "", "summary": "The open .blend, its Blender version, frame range and fps."},
            {"op": "scene.objects", "args": "", "summary": "Every object: name, type, transform, and whether it is animated."},
            {"op": "object.get", "args": "name", "summary": "One object in full, with its mesh counts, material and action."},
            {"op": "object.set", "args": "name, [location], [rotation_euler], [scale]", "summary": "Move, turn or scale an object."},
            {"op": "action.get", "args": "name", "summary": "An object's curves: data path, index, keyframes and interpolation."},
            {"op": "frame.set", "args": "frame", "summary": "Move the playhead, so a later read sees that pose."},
            {"op": "export", "args": "out, [only]", "summary": "Run the ae3d exporter over the open file."},
            {"op": "quit", "args": "", "summary": "Stop serving."},
        ],
    }


def op_ping(_request):
    return {}


def op_file_info(_request):
    scene = bpy.context.scene
    return {
        "filepath": bpy.data.filepath,
        "blender": bpy.app.version_string,
        "scene": scene.name,
        "frame_start": scene.frame_start,
        "frame_end": scene.frame_end,
        "frame_current": scene.frame_current,
        "fps": scene.render.fps,
        "fps_base": scene.render.fps_base,
        "objects": len(scene.objects),
    }


def object_brief(obj):
    return {
        "name": obj.name,
        "type": obj.type,
        "location": list(obj.location),
        "rotation_mode": obj.rotation_mode,
        "scale": list(obj.scale),
        "animated": bool(obj.animation_data and obj.animation_data.action),
        "hidden": not obj.visible_get() if obj.name in bpy.context.view_layer.objects else None,
    }


def op_scene_objects(_request):
    objects = sorted(bpy.context.scene.objects, key=lambda o: o.name)
    return {"count": len(objects), "objects": [object_brief(o) for o in objects]}


def find_object(request):
    name = request.get("name")
    if not name:
        raise ValueError("this op needs a name")
    obj = bpy.data.objects.get(name)
    if obj is None:
        raise ValueError("no object called %r" % name)
    return obj


def op_object_get(request):
    obj = find_object(request)
    out = object_brief(obj)
    out["rotation_euler"] = list(obj.rotation_euler)
    out["materials"] = [m.name for m in obj.data.materials] if getattr(obj.data, "materials", None) else []
    if obj.type == "MESH":
        out["vertices"] = len(obj.data.vertices)
        out["polygons"] = len(obj.data.polygons)
    if obj.animation_data and obj.animation_data.action:
        out["action"] = obj.animation_data.action.name
    return out


def op_object_set(request):
    obj = find_object(request)
    for field in ("location", "rotation_euler", "scale"):
        value = request.get(field)
        if value is not None:
            setattr(obj, field, tuple(float(v) for v in value))
    bpy.context.view_layer.update()
    return op_object_get(request)


def iter_fcurves(action):
    layers = getattr(action, "layers", None)
    if layers:
        for layer in layers:
            for strip in layer.strips:
                for bag in getattr(strip, "channelbags", []):
                    for fcurve in bag.fcurves:
                        yield fcurve
        return
    for fcurve in getattr(action, "fcurves", []):
        yield fcurve


def op_action_get(request):
    obj = find_object(request)
    if not obj.animation_data or not obj.animation_data.action:
        raise ValueError("%r has no action" % obj.name)
    action = obj.animation_data.action
    curves = []
    for fcurve in iter_fcurves(action):
        curves.append({
            "data_path": fcurve.data_path,
            "index": fcurve.array_index,
            "keys": [
                {
                    "frame": key.co[0],
                    "value": key.co[1],
                    "interpolation": key.interpolation,
                    "handle_left": list(key.handle_left),
                    "handle_right": list(key.handle_right),
                }
                for key in fcurve.keyframe_points
            ],
        })
    return {"action": action.name, "curves": curves}


def op_frame_set(request):
    frame = int(request.get("frame", bpy.context.scene.frame_current))
    bpy.context.scene.frame_set(frame)
    return {"frame": bpy.context.scene.frame_current}


def op_export(request):
    out = request.get("out")
    if not out:
        raise ValueError("export needs an out directory")

    here = os.path.dirname(os.path.abspath(__file__))
    exporter = os.path.join(here, "ae3d_export.py")
    namespace = {"__file__": exporter, "__name__": "ae3d_export_invoked"}
    with open(exporter) as handle:
        code = compile(handle.read(), exporter, "exec")
    exec(code, namespace)

    argv = ["--out", out]
    only = request.get("only")
    if only:
        argv += ["--only", only]
    namespace["main"](argv)
    return {"out": out}


OPS = {
    "help": op_help,
    "ping": op_ping,
    "file.info": op_file_info,
    "scene.objects": op_scene_objects,
    "object.get": op_object_get,
    "object.set": op_object_set,
    "action.get": op_action_get,
    "frame.set": op_frame_set,
    "export": op_export,
}


def respond(payload):
    global _client
    if _client is None:
        return
    try:
        _client.sendall((json.dumps(payload) + "\n").encode("utf-8"))
    except OSError:
        _client = None


def serve_one(line):
    global _running
    try:
        request = json.loads(line)
    except ValueError:
        respond({"id": 0, "ok": False, "error": "not JSON"})
        return

    request_id = request.get("id", 0)
    op = request.get("op")
    if op == "quit":
        respond({"id": request_id, "ok": True, "result": {}})
        _running = False
        return
    handler = OPS.get(op)
    if handler is None:
        respond({"id": request_id, "ok": False,
                 "error": "unknown op; send {\"op\":\"help\"} for the table"})
        return
    try:
        respond({"id": request_id, "ok": True, "result": handler(request)})
    except Exception as failure:
        respond({"id": request_id, "ok": False, "error": str(failure)})


def drain():
    while True:
        try:
            line = _requests.get_nowait()
        except queue.Empty:
            return
        serve_one(line)


def accept_loop(listener):
    global _client
    while _running:
        try:
            client, _ = listener.accept()
        except OSError:
            return
        _client = client
        buffer = b""
        while _running:
            try:
                chunk = client.recv(4096)
            except OSError:
                break
            if not chunk:
                break
            buffer += chunk
            while b"\n" in buffer:
                line, buffer = buffer.split(b"\n", 1)
                if line.strip():
                    _requests.put(line.decode("utf-8", "replace"))
        try:
            client.close()
        except OSError:
            pass
        _client = None


def start(port):
    global _listener, _running
    _listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    _listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    _listener.bind(("127.0.0.1", port))
    _listener.listen(4)
    _running = True
    threading.Thread(target=accept_loop, args=(_listener,), daemon=True).start()
    chosen = _listener.getsockname()[1]
    print("ae3d: blender agent channel on 127.0.0.1:%d" % chosen, flush=True)
    return chosen


def main(argv):
    parser = argparse.ArgumentParser(prog="ae3d_agent_server")
    parser.add_argument("--port", type=int, default=0)
    parser.add_argument("--port-file", default=None)
    parser.add_argument("--serve", type=float, default=0.0,
                        help="seconds to pump requests directly, for --background")
    args = parser.parse_args(argv)

    chosen = start(args.port)
    if args.port_file:
        with open(args.port_file, "w") as handle:
            handle.write("%d\n" % chosen)

    if args.serve > 0.0:
        deadline = time.time() + args.serve
        while _running and time.time() < deadline:
            drain()
            time.sleep(0.02)
        drain()
        return 0

    bpy.app.timers.register(pump_timer, first_interval=0.05)
    return 0


def pump_timer():
    drain()
    return 0.05 if _running else None


if __name__ == "__main__":
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    sys.exit(main(argv))
