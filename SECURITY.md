# Security Policy

## Reporting a Vulnerability

If you discover a security vulnerability in ETIL, please report it responsibly.

**Do NOT open a public GitHub issue for security vulnerabilities.**

Instead, please email: **evolutionary-til-support@googlegroups.com**

Include:
- Description of the vulnerability
- Steps to reproduce
- Potential impact
- Suggested fix (if any)

You should receive a response within 72 hours. I take security seriously — ETIL
runs a public MCP server with authentication, so vulnerabilities have real impact.

## Scope

The following are in scope for security reports:

- **MCP server** (`examples/mcp_server.cpp`, `src/mcp/`) — authentication bypass,
  authorization escalation, remote code execution, denial of service
- **Interpreter** (`src/core/`) — memory corruption, stack overflow, sandbox escape
- **TUI client** (`tools/mcp-client/`) — credential leakage, injection attacks
- **Docker configuration** — container escape, privilege escalation
- **nginx configuration** (`deploy/`) — TLS misconfiguration, request smuggling
- **OAuth/JWT** (`src/mcp/jwt_auth.cpp`, `src/mcp/oauth_*.cpp`) — token forgery,
  authentication bypass

## Security Architecture

ETIL follows defense-in-depth:

1. **nginx** — TLS termination, rate limiting, connection limits, request size caps
2. **Docker** — Read-only filesystem, no-new-privileges, CPU/memory/PID limits
3. **Application** — Instruction budget, execution deadline, call depth limits,
   output size caps, URL allowlisting, SSRF protection
4. **Authentication** — JWT (RS256) with role-based access control, OAuth device flow
5. **Monitoring** — Runtime security monitoring recommended (e.g., Falco eBPF)

## Supported Versions

Only the latest release is supported with security updates.
