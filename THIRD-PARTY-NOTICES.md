# Third-party notices

Inkling is published by JSSL under its own licence (LICENSE.md). It includes the third-party components below, each
under its own licence. Their full licence texts also come with every download, in the `inkling/licences` folder:
`cJSON-LICENSE`, `Monocypher-LICENCE.md`, `stb-LICENSE`, `CURL-MBEDTLS-LICENSES.txt` (curl's licence and the Apache License 2.0),
`OFL.txt`, `HERSHEY-NOTICE.txt` and `FONT-CREDITS.txt`.

| Component | Version | What Inkling uses it for | Licence |
|---|---|---|---|
| cJSON | 1.7.19 | reading and writing JSON | MIT |
| Monocypher (with its Ed25519 add-on) | 4.0.2 | checking the signature of updates | BSD-2-Clause or CC0-1.0 (dual) |
| stb_truetype | 1.26 | drawing typed text with the Kindle's fonts | public domain or MIT (dual) |
| stb_image | 2.30 | showing saved pages (PNG) | public domain or MIT (dual) |
| stb_image_write | 1.16 | saving pages as PNG images | public domain or MIT (dual) |
| musl libc | as bundled with Zig 0.14.1 | the C library built into both programs | MIT |
| Zig compiler runtime | 0.14.1 | low-level runtime routines built into both programs | MIT |
| curl | 8.23.0 | the email helper (`curl-smtp`) | curl licence (MIT-style) |
| Mbed TLS | 3.6.7 | encryption (TLS) for the email helper | Apache-2.0 (chosen from Apache-2.0 or GPL-2.0-or-later) |
| Hershey fonts | Hurt (.jhf) format | 9 handwriting styles | Hershey font notice (below) |
| EMS single-line fonts | from gitlab.com/oskay/svg-fonts | 21 handwriting styles | SIL Open Font License 1.1 |
| 12dicts word lists (3of6game, 3esl) | 6.0.2 | the word games | public domain |

Inkling also uses things that are already on your Kindle and are not part of the download: the Kindle's own fonts
(for typed text) and its `curl` program (for talking to AI services and GitHub).

---

## cJSON

Copyright (c) 2009-2017 Dave Gamble and cJSON contributors

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated
documentation files (the "Software"), to deal in the Software without restriction, including without limitation the
rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit
persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the
Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

## Monocypher

Monocypher is dual-licensed under the 2-clause BSD licence and CC0-1.0; Inkling includes the BSD notice:

Copyright (c) 2017-2023, Loup Vaillant
Copyright (c) 2017-2019, Michael Savage
Copyright (c) 2017-2023, Fabio Scotoni
All rights reserved.

Redistribution and use in source and binary forms, with or without modification, are permitted provided that the
following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following
   disclaimer.
2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following
   disclaimer in the documentation and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

## stb_truetype, stb_image, stb_image_write

stb_truetype, stb_image and stb_image_write by Sean Barrett (nothings.org), used under their public-domain option.
No notice is required; they are credited here with thanks (their licence text is in `stb-LICENSE`).

## musl libc

Copyright © 2005-2020 Rich Felker, et al.

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated
documentation files (the "Software"), to deal in the Software without restriction, including without limitation the
rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit
persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the
Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

## Zig compiler runtime

Copyright (c) Zig contributors

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated
documentation files (the "Software"), to deal in the Software without restriction, including without limitation the
rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit
persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the
Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

## curl

COPYRIGHT AND PERMISSION NOTICE

Copyright (c) 1996 - 2026, Daniel Stenberg, <daniel@haxx.se>, and many contributors, see the THANKS file.

All rights reserved.

Permission to use, copy, modify, and distribute this software for any purpose with or without fee is hereby granted,
provided that the above copyright notice and this permission notice appear in all copies.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT OF THIRD PARTY RIGHTS. IN NO EVENT
SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.

Except as contained in this notice, the name of a copyright holder shall not be used in advertising or otherwise to
promote the sale, use or other dealings in this Software without prior written authorization of the copyright holder.

## Mbed TLS

Mbed TLS (https://www.trustedfirmware.org/projects/mbed-tls/) is provided under Apache-2.0 OR GPL-2.0-or-later;
Inkling uses it under the Apache License, Version 2.0. Copyright The Mbed TLS Contributors. The full licence text is
in `inkling/licences/CURL-MBEDTLS-LICENSES.txt` and at https://www.apache.org/licenses/LICENSE-2.0.

Changes: Inkling's copy is built from the unmodified 3.6.7 release with a changed configuration header
(`include/mbedtls/mbedtls_config.h`) that turns off features the email helper doesn't use (for example DTLS). No other
file is changed.

## Hershey fonts

Nine handwriting styles (Script Simplex, Script Complex, Cursive, Simplex Roman, Duplex Roman, Times Roman, Times
Italic, Gothic English and Gothic Italian) are converted from the Hershey fonts in the James Hurt (.jhf) format. Their
distribution terms require these acknowledgements:

- The Hershey Fonts were originally created by Dr. A. V. Hershey while working at the U. S. National Bureau of
  Standards.
- The format of the Font data in this distribution was originally created by James Hurt, Cognition, Inc., 900
  Technology Park Drive, Billerica, MA 01821 (mit-eddie!ci-dandelion!hurt).

The full notice, including the use terms, is in `inkling/licences/HERSHEY-NOTICE.txt`.

## EMS single-line fonts

Twenty-one handwriting styles are converted from the EMS single-line SVG fonts: single-line versions created by
Sheldon B. Michaels, SVG font conversion by Windell H. Oskay (Evil Mad Scientist Laboratories), from fonts licensed
under the SIL Open Font License 1.1 (source: https://gitlab.com/oskay/svg-fonts). `inkling/licences/FONT-CREDITS.txt`
lists each one with the font it derives from and that font's designer: EMS Allure (Allura), EMS Brush (Alex Brush),
EMS Capitol (Sacramento), EMS Casual Hand (Covered By Your Grace), EMS Decorous Script (Petit Formal Script), EMS
Delight (Delius), EMS Elfin (Mountains of Christmas), EMS Felix (Felipa), EMS Herculean (Poiret One), EMS Invite
(Tangerine), EMS League (League Script), EMS Little Princess (Princess Sofia), EMS Neato (Bad Script), EMS Nixish
(Nixie One), EMS Osmotron (Orbitron), EMS Pancakes (Short Stack), EMS Readability and EMS Readability Italic (Source
Sans Pro Light), EMS Society (Mrs Saint Delafield), EMS Swiss (Italianno) and EMS Tech (Architects Daughter).

The converted font data built into Inkling is a Modified Version of these fonts. It is distributed under the SIL Open
Font License 1.1 (full text in `inkling/licences/OFL.txt`), is not offered or sold on its own, and is not given any of
the original font names in the app. EMS Elfin's parent font, Mountains of Christmas, is also published under the
Apache License, Version 2.0.

## 12dicts word lists

The word games use the 3of6game and 3esl lists from 12dicts 6.0.2 by Alan Beale, who released them to the public
domain and asks for acknowledgement of their use: thank you.
