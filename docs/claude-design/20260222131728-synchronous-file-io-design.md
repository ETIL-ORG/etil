# ETIL Synchronous File I/O Design
- Dated 20280222

## Synchronous File Function

- Read & Write whole files to/from string.
- All functions block until success, failure, or word/session timeouts.
- Functions shown below can only write to the /home/** LVFS directory.

### Function List
- appendFileSync   ( string path -- flag )
- copyFileSync     ( src dest -- flag )
- existsSync       ( path -- flag )
- lstatSync        ( path -- array? flag )
- mkdirSync        ( path -- flag )
- mkdirTmpSync     ( prefix -- string? flag )
- readdirSync      ( path -- array? flag )
- readFileSync     ( path -- string? flag )
- renameSync       ( oldPath newPath -- flag )
- rmdirSync        ( path -- flag )
- rmSync           ( path -- flag )
- truncateSync     ( path -- flag )
- writeFileSync    ( string path -- flag )
