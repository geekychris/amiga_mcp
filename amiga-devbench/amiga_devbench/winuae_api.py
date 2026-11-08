# SPDX-License-Identifier: MIT
"""HTTP adapter; all debugger policy and serialization live in the backend."""
import json
from typing import Any, Awaitable, Callable
from urllib.parse import urlsplit
from starlette.requests import Request
from starlette.responses import JSONResponse
from .winuae_debug import WinuaeDebugger, DebugError


def make_winuae_endpoint(debugger: WinuaeDebugger,
                         host_actions: dict[str, Callable[..., Awaitable[Any]]] | None = None):
    """host_actions are operations that need more of DevBench than the
    debugger, such as debug_run (build, deploy and launch)."""
    host_actions = host_actions or {}

    async def endpoint(request: Request) -> JSONResponse:
        action = request.path_params['action']
        # Browsers send Origin with cross-site requests; refuse those, so a web
        # page cannot drive the debugger. Scripts without Origin still work.
        origin = request.headers.get('origin')
        if origin and urlsplit(origin).netloc != request.headers.get('host'):
            return JSONResponse({'ok': False, 'error': 'Cross-origin requests are not allowed'}, status_code=403)
        if request.method == 'GET' and action != 'status':
            return JSONResponse({'ok': False, 'error': 'Use POST for debugger operations'}, status_code=405)
        try:
            body = bytearray()
            async for chunk in request.stream():
                if len(body) + len(chunk) > 16384:
                    return JSONResponse({'ok': False, 'error': 'Request too large'}, status_code=413)
                body.extend(chunk)
            args = json.loads(body) if body else {}
            if not isinstance(args, dict) or 'action' in args:
                raise DebugError('Expected an object of operation parameters')
            if action in host_actions:
                result = await host_actions[action](**args)
            else:
                result = await debugger.call(action, **args)
            return JSONResponse({'ok': True, 'result': result})
        except (DebugError, ValueError, TypeError) as exc:
            return JSONResponse({'ok': False, 'error': str(exc)}, status_code=400)
    return endpoint
