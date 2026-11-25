"""Minimal local Streamable HTTP MCP client. No fixed editor port or toolset name."""
import json
import urllib.error
import urllib.request


class Client:
    def __init__(self, port):
        self.url = f"http://127.0.0.1:{int(port)}/mcp"
        self.session = None
        self.index = 0
        self.request("initialize", {"protocolVersion": "2025-06-18", "capabilities": {},
                                    "clientInfo": {"name": "physics-asset-migration", "version": "1"}})
        self.request("notifications/initialized", {}, notification=True)

    def request(self, method, params, notification=False):
        self.index += 1
        payload = {"jsonrpc": "2.0", "method": method, "params": params}
        if not notification:
            payload["id"] = self.index
        headers = {"Content-Type": "application/json", "Accept": "application/json, text/event-stream"}
        if self.session:
            headers["Mcp-Session-Id"] = self.session
        request = urllib.request.Request(self.url, json.dumps(payload).encode(), headers)
        try:
            with urllib.request.urlopen(request, timeout=55) as response:
                self.session = response.headers.get("Mcp-Session-Id", self.session)
                body = response.read().decode("utf-8")
        except urllib.error.HTTPError as error:
            raise RuntimeError(f"MCP HTTP {error.code}: {error.read().decode('utf-8', errors='replace')}") from error
        if not body:
            return None
        if body.lstrip().startswith(("event:", "data:", ":")):
            messages = []
            for block in body.replace("\r\n", "\n").split("\n\n"):
                data = "\n".join(line[5:].lstrip() for line in block.splitlines() if line.startswith("data:"))
                if data:
                    messages.append(json.loads(data))
            matching = [m for m in messages if m.get("id") == payload.get("id")]
            if not matching:
                if notification:
                    return None
                raise RuntimeError("MCP stream has no matching response")
            result = matching[-1]
        else:
            result = json.loads(body)
        if "error" in result:
            raise RuntimeError(str(result["error"]))
        return result.get("result")

    def tool(self, name, arguments):
        result = self.request("tools/call", {"name": name, "arguments": arguments})
        if result.get("isError"):
            raise RuntimeError(json.dumps(result, ensure_ascii=False))
        return result

    def call(self, toolset, name, arguments):
        return self.tool("call_tool", {"toolset_name": toolset, "tool_name": name, "arguments": arguments})

    def worker(self, stage, job_file):
        listing = self.tool("list_toolsets", {})
        lines = "\n".join(c.get("text", "") for c in listing.get("content", []))
        names = [line[2:].split(":", 1)[0] for line in lines.splitlines()
                 if line.startswith("- ") and "AgentPhysicsAssetTools" in line.split(":", 1)[0]]
        if len(names) != 1:
            raise RuntimeError(f"Expected one temporary migration toolset; found {names}. Bootstrap unreal_worker.py once in this editor.")
        return self.call(names[0], "run", {"stage": stage, "job_file": str(job_file)})

    def close(self):
        if self.session:
            request = urllib.request.Request(self.url, method="DELETE", headers={"Mcp-Session-Id": self.session})
            try:
                with urllib.request.urlopen(request, timeout=5):
                    pass
            except (urllib.error.URLError, OSError):
                pass
            self.session = None

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()
