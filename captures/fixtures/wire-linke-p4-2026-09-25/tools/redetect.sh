#!/bin/bash
exec uv run --no-project --with pyusb python "$(dirname "$0")/redetect.py" "$@"
