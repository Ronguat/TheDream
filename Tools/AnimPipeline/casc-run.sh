#!/usr/bin/env bash
# Runs a Python file inside the open Cascadeur through its bundled script server
# (127.0.0.1:8765; start it with `cascadeur.exe --run-script scripts.mcp.start_server`) and prints
# the event-log lines it produced. Usage: casc-run.sh script.py  |  casc-run.sh -c "code"
set -u
if [ "${1:-}" = "-c" ]; then CODE="$2"; else CODE="$(cat "$1")"; fi
BODY=$(printf '%s' "$CODE" | perl -MJSON::PP -0777 -ne 'print JSON::PP->new->encode({code=>$_})')
RESP=$(curl -s -m "${CASC_TIMEOUT:-60}" -X POST http://127.0.0.1:8765/run \
  -H "Content-Type: application/json" --data-binary "$BODY")
printf '%s' "$RESP" | perl -MJSON::PP -0777 -ne '
  my $r = JSON::PP->new->decode($_);
  for my $m (@{$r->{messages} || []}) { print "[$m->{level}] $m->{text}\n"; }
  print "value: $r->{value}\n" if defined $r->{value} && length $r->{value};
  if (!$r->{ok}) { print STDERR "ERROR: $r->{error}\n"; exit 1; }'
