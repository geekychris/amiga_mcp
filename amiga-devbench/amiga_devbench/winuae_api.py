# SPDX-License-Identifier: MIT
"""HTTP adapter; all debugger policy and serialization live in the backend."""
import json
from starlette.requests import Request
from starlette.responses import JSONResponse
from .winuae_debug import WinuaeDebugger, DebugError


def make_winuae_endpoint(debugger: WinuaeDebugger):
    async def endpoint(request: Request) -> JSONResponse:
        action = request.path_params['action']
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
            result = await debugger.call(action, **args)
            return JSONResponse({'ok': True, 'result': result})
        except (DebugError, ValueError, TypeError) as exc:
            return JSONResponse({'ok': False, 'error': str(exc)}, status_code=400)
    return endpoint
