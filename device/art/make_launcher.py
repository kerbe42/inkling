#!/usr/bin/env python3
"""Builds the library entry 'inkling.sh' with its title, subtitle and embedded icon (shell-integration header)."""
import base64, pathlib
HERE = pathlib.Path(__file__).parent
icon = base64.b64encode((HERE / 'icon.png').read_bytes()).decode()
body = (
    "#!/bin/sh\n"
    "# Name: Inkling\n"
    "# Author: your notebook that writes back\n"
    f"# Icon: data:image/png;base64,{icon}\n"
    "# DontUseFBInk\n"
    "# Library entry for Inkling: tap it to open the writing app. (Copied to /mnt/us/documents/inkling.sh.)\n"
    "exec /bin/sh /var/local/scribe/app/launch.sh\n"
)
(HERE.parent / 'app' / 'inkling.sh').write_text(body)
print('inkling.sh', len(body), 'bytes')
