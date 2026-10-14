import asyncio
import json
from types import SimpleNamespace

import httpx
import pytest
from starlette.applications import Starlette
from starlette.routing import Route

from amiga_devbench.config import load_config, save_config, apply_profile
from amiga_devbench.winuae_api import make_winuae_endpoint
from amiga_devbench.winuae_debug import WinuaeDebugger
from amiga_devbench.winuae_tools import init_winuae_tools


def test_configuration_defaults_profiles_and_round_trip(tmp_path):
    path = tmp_path/'devbench.toml'
    path.write_text('''[winuae_debug]
enabled = false
host = "localhost"
port = 2346
[profiles.winuae]
winuae_debug_enabled = true
[profiles.fsuae]
winuae_debug_enabled = false
''')
    cfg = load_config(str(path))
    assert not cfg.winuae_debug_enabled
    apply_profile(cfg, 'winuae')
    assert cfg.winuae_debug_enabled and cfg.winuae_debug_port == 2346
    save_config(cfg, str(path))
    copy = load_config(str(path))
    assert copy.winuae_debug_enabled and copy.winuae_debug_host == 'localhost'
    apply_profile(copy, 'fsuae')
    assert not copy.winuae_debug_enabled
    for invalid in ['enabled = "false"', 'port = 0', 'host = ""']:
        path.write_text('[winuae_debug]\n'+invalid)
        with pytest.raises(ValueError): load_config(str(path))


def test_http_validation_and_no_implicit_connection():
    async def run():
        backend = WinuaeDebugger()
        app = Starlette(routes=[Route('/api/winuae/{action}', make_winuae_endpoint(backend), methods=['GET','POST'])])
        async with httpx.AsyncClient(transport=httpx.ASGITransport(app=app), base_url='http://test') as c:
            response = await c.get('/api/winuae/status')
            assert response.json()['result']['execution'] == 'disconnected'
            assert (await c.get('/api/winuae/connect')).status_code == 405
            assert (await c.post('/api/winuae/connect')).status_code == 400
            for body in ['invalid', '[]', '{"action":"connect"}']:
                assert (await c.post('/api/winuae/status', content=body)).status_code == 400
            assert (await c.post('/api/winuae/status', content=' '*16385)).status_code == 413
            assert not backend.connected
    asyncio.run(run())


def test_mcp_and_http_use_same_backend():
    from amiga_devbench.mcp_tools import mcp
    async def run():
        class Backend:
            def __init__(self): self.calls = []
            async def call(self, action, **args):
                self.calls.append((action,args))
                return {'execution':'stopped', 'marker':'shared'}
        backend = Backend()
        init_winuae_tools(backend)
        app = Starlette(routes=[Route('/api/winuae/{action}', make_winuae_endpoint(backend), methods=['POST'])])
        try:
            result = await mcp.call_tool('amiga_winuae_control', {'action':'pause'})
            assert 'shared' in str(result)
            async with httpx.AsyncClient(transport=httpx.ASGITransport(app=app), base_url='http://test') as c:
                assert (await c.post('/api/winuae/registers')).json()['result']['marker'] == 'shared'
            assert [call[0] for call in backend.calls] == ['pause','registers']
        finally:
            init_winuae_tools(WinuaeDebugger())
    asyncio.run(run())
