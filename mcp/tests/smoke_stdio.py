"""Start both servers over stdio with a real MCP client: list tools, call a few."""
import asyncio, sys
from pathlib import Path
from mcp.client.stdio import stdio_client, StdioServerParameters
from mcp.client.session import ClientSession

BIN = Path(__file__).resolve().parents[1] / ".venv" / "bin"

async def probe(server, calls):
    params = StdioServerParameters(command=str(BIN / server), args=[])
    async with stdio_client(params) as (r, w):
        async with ClientSession(r, w) as s:
            await s.initialize()
            tools = await s.list_tools()
            print(server, "tools:", ", ".join(t.name for t in tools.tools))
            for name, args in calls:
                res = await s.call_tool(name, args)
                text = res.content[0].text if res.content else str(res)
                print(f"  {name}({args}) -> {text[:160]!r}")

async def main():
    await probe("frank-docs-mcp", [("pin", {"board_id": "NA", "gpio": 43}),
                                   ("list_boards", {}), ("console_commands", {})])
    if "--bench" in sys.argv:
        await probe("frank-dev-mcp", [("command", {"board": "NA", "cmd": "psram"})])
    else:
        await probe("frank-dev-mcp", [])

asyncio.run(main())
