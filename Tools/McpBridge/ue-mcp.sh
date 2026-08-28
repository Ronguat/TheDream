#!/usr/bin/env bash
# MCP client over HTTP for the Unreal editor plugin at 127.0.0.1:8000.
#
#   ue-mcp.sh list                             # list_toolsets
#   ue-mcp.sh describe <toolset>               # describe_toolset
#   ue-mcp.sh call <toolset> <tool> [args]     # call_tool; args is JSON, default {}
#   ue-mcp.sh rpc <method> <params-json>       # any JSON-RPC method
#
# Negotiates a session on demand, caches its id, and re-negotiates once when the server
# rejects it. The registered mcp__unreal-mcp__* tools cover the same surface.
set -u
URL="${UE_MCP_URL:-http://127.0.0.1:8000/mcp}"
SID_FILE="${TMPDIR:-/tmp}/td-ue-mcp.sid"
CT="Content-Type: application/json"
ACCEPT="Accept: application/json, text/event-stream"

# $1=body $2=session id, empty for the handshake
post() {
  if [ -n "$2" ]; then
    curl -s -m "${UE_MCP_TIMEOUT:-60}" -X POST "$URL" -H "$CT" -H "$ACCEPT" \
      -H "Mcp-Session-Id: $2" --data-binary "$1"
  else
    curl -s -m "${UE_MCP_TIMEOUT:-60}" -X POST "$URL" -H "$CT" -H "$ACCEPT" --data-binary "$1"
  fi
}

# Prints the negotiated session id; the id arrives as a response header, not in the body.
init() {
  local hdrs sid
  hdrs=$(curl -s -i -m 15 -X POST "$URL" -H "$CT" -H "$ACCEPT" --data-binary \
    '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-06-18","capabilities":{},"clientInfo":{"name":"ue-mcp.sh","version":"1"}}}')
  sid=$(printf '%s' "$hdrs" | grep -i '^Mcp-Session-Id:' | sed 's/.*: *//' | tr -d '\r\n')
  [ -n "$sid" ] || { echo "ue-mcp: no session id from $URL" >&2; return 1; }
  printf '%s' "$sid" > "$SID_FILE"
  post '{"jsonrpc":"2.0","method":"notifications/initialized"}' "$sid" >/dev/null
  printf '%s' "$sid"
}

# $1=method $2=params-json
rpc() {
  local sid body out
  sid=$(cat "$SID_FILE" 2>/dev/null || true)
  body="{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"$1\",\"params\":$2}"
  [ -n "$sid" ] || sid=$(init) || return 1
  out=$(post "$body" "$sid")
  case "$out" in
    ''|*'Unknown session id'*|*'should reinitialize'*)
      sid=$(init) || return 1
      out=$(post "$body" "$sid") ;;
  esac
  printf '%s\n' "$out"
}

ARGS="${4:-}"; [ -n "$ARGS" ] || ARGS='{}'
case "${1:-}" in
  list)     rpc tools/call '{"name":"list_toolsets","arguments":{}}' ;;
  describe) rpc tools/call "{\"name\":\"describe_toolset\",\"arguments\":{\"toolset_name\":\"$2\"}}" ;;
  call)     rpc tools/call "{\"name\":\"call_tool\",\"arguments\":{\"toolset_name\":\"$2\",\"tool_name\":\"$3\",\"arguments\":$ARGS}}" ;;
  rpc)      rpc "$2" "${3:-{\}}" ;;
  *)        sed -n '2,10p' "$0"; exit 2 ;;
esac
