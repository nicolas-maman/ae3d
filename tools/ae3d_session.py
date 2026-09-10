"""A kept connection to a running ae3d or Blender.

    from ae3d_session import Session
    with Session(7911) as engine:
        engine.frame_pause()
        scene = engine("scene.tree", detail=True)

Both ends speak the same protocol, so the same class drives the engine and
Blender, and the only difference is which port it is pointed at.

Why this exists. tools/ae3d_agent.py answers one question per process: it
starts Python, opens a socket, writes a line, reads a line and exits, which
costs about fifty milliseconds of which six are the question. That is fine for
asking one thing from a shell and wrong for the loop an agent actually runs,
where a measurement is hundreds of questions. Keeping the socket is eight
times faster per call, and the calls that remain cost what they should: one
frame, because the engine answers in its own pump and the answer describes a
frame that has to have been drawn.

The other half of going fast is not asking so often -- `scene.tree detail`
answers for every model at once rather than one round trip each -- and this
class is what makes that worth having.
"""

import json
import socket


class AgentError(RuntimeError):
    """The far end answered, and the answer was no."""


class Session:
    def __init__(self, port, host="127.0.0.1", timeout=30.0):
        self.socket = socket.create_connection((host, port), timeout)
        self.socket.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        self.stream = self.socket.makefile("rwb")
        self.next_id = 1

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()

    def __call__(self, op, **arguments):
        """Ask, and answer with the result. Raises when the far end says no."""
        arguments["op"] = op
        arguments["id"] = self.next_id
        self.next_id += 1
        self.stream.write((json.dumps(arguments) + "\n").encode("utf-8"))
        self.stream.flush()
        line = self.stream.readline()
        if not line:
            raise AgentError("%s: the connection closed" % op)
        answered = json.loads(line.decode("utf-8"))
        if not answered.get("ok"):
            raise AgentError("%s: %s" % (op, answered.get("error", "no reason given")))
        return answered.get("result", {})

    def __getattr__(self, name):
        """session.frame_pause() is session("frame.pause")."""
        if name.startswith("_"):
            raise AttributeError(name)
        op = name.replace("_", ".", 1)
        return lambda **arguments: self(op, **arguments)

    def close(self):
        if self.stream is not None:
            try:
                self.stream.close()
            except OSError:
                pass
            self.stream = None
        if self.socket is not None:
            try:
                self.socket.close()
            except OSError:
                pass
            self.socket = None
