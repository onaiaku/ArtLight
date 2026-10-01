#!/bin/sh

PORT=47990

if ! curl -k https://localhost:$PORT > /dev/null 2>&1; then
  (sleep 3 && xdg-open https://localhost:$PORT) &
  exec artlight "$@"
else
  echo "ArtLight is already running, opening the configuration API..."
  xdg-open https://localhost:$PORT
fi
