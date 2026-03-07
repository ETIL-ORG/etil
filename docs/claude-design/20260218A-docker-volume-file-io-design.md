# My original Docker File I/O Plan:

**We're going to take a much bigger approach to this.
This `/load` problem is a one symptom of a set of problems related to File I/O
in the contained environment. 

Let me set a policy now...

## RULE: The MCP server, regardless of transport, will not be allowed to perform any kind of File I/O outside the container.

- No Docker bind mounts are allowed.
- The only exception is the use of Docker volumes that are _not_ shared between containers.
- Everything should be contained in the docker images as much as possible.
  - Easier deployment .vs. increased build time.

## RULE: NO FILE UPLOADING: Except via existing TUI `/load` command.

## DETAILS:
- I want to use Docker Volumes (not OS bind mounts) to
  supply the file system that the MCP servers see in the contained environments.
  Furthermore, the file I/O needs separation between different sessions on the MCP
  server.
- I propose creating individual `home` directories for each session in the
  Docker volume, and a session's read/write file I/O be restricted to and under that
  `home` directory.
- The interpreter will have to perform logical file path mapping to
  make it appear to the session it is at `/home` and there are no other user's
  session visible.
- To address the TUI `/load` problem... It then becomes an issue of
  having a 'library' of .til files that can be included from another separate Docker
  volume that provide the necessary include file.
- This takes on the aspect of
  creating a 'standard library' of ETIL code that can be included. The current
  `include` word functionality word be reworked to use the logical paths to the the
  read/write `/home`.
- Another word like `library` would be used to load ETIL library
  code from a logical read-only `/library` mount point and by specifying the
  relative path inside the `/library` mount point.**

