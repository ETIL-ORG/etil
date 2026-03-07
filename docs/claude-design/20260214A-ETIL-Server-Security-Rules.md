# ETIL Server Security Rules

# General Rules:

1. This list of rules is not exhaustive.
2. _**Always review design and code changes to avoid vulnerabilities
   allowing malicious (or just stupid) actors to gain entry to the system as a whole.**_
3. When in doubt ask me.

## Network Access: General

1. Any executable or script produced or used by the ETIL project
   that will expose any active listening network interface
   MUST run that executable in a sandboxed environment that
   minimizes access to the host system.
2. The prohibition in (1) includes the `localhost` interface and any loopback interfaces.

## Running as a MCP Server

1. Any ETIL executable or script exposing any MCP transport or interface
   MUST run that executable in a sandboxed environment that
   minimizes access to the host system.
2. The prohibition in (1) extends to any testing code, scripts, or environments.
3. (1) and (2) slow down testing but those rules are ABSOLUTE.
4. To speed testing in layered containers like Docker optimize the layering so that
   the container image puts the files that change the most often
   as the last layers in the container. 
