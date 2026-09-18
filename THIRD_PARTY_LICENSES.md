# Third party licenses

## Gopher3D

ae3d is a port of Gopher3D. See [NOTICE](NOTICE) for what was taken from it.

```
MIT License

Copyright (c) 2024-2026 Gopher3D contributors

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

## stb_image

`native/stb_image.h` is vendored verbatim from https://github.com/nothings/stb.
It is dual licensed as MIT and public domain; ae3d uses it under the MIT
terms.

```
MIT License

Copyright (c) 2017 Sean Barrett

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
of the Software, and to permit persons to whom the Software is furnished to do
so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

## Model resources

`resources/obj/` carries the demo models from Gopher3D's `examples/resources`,
under the Gopher3D licence above.

## GLFW and Vulkan

GLFW (zlib/libpng) and the Vulkan loader and MoltenVK (Apache 2.0) are system
dependencies. Neither is vendored here; both are linked or opened at runtime from
the copies the platform provides.

## Fox (tests/fixtures/gltf/Fox.glb)

The glTF sample model the loader is tested against, from the Khronos
[glTF-Sample-Assets](https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/Fox)
repository, unchanged.

- Model: © 2014 PixelMannen, [CC0 1.0](https://creativecommons.org/publicdomain/zero/1.0/legalcode).
- Rigging and animation: © 2014 tomkranis, [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/legalcode).
- Conversion to glTF: © 2017 @AsoboStudio and @scurest, [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/legalcode).
