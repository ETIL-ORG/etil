# ETIL REPL Enhancement Plan "SnowBound One"
- Its called "SnowBound One" because I'm spending a log of time indoors `cuz it won't physically stop snowing here.

## REPL OS Command Line Options and Parameter Handling.
- The REPL shall use `getopt long` style command line processing using short/long option processing.
- All the non-option parameters suffixing the command line shall be passed to the interpreter via the REPL on startup.
- If a parameter is the literal dash character `-` then all following interpreter input shall be read from stdin 
until stdin reports EOF.

## ETIL Configuration Persistence: Configuration Files and Directories
- All configuration files will be in JSON.
- Top Level Configuration Path: The hidden directory `.etil` will be the top level directory
for all etil related configuration files.
- The interpreter (eventually, not now) will store configuration in directory `.etil/interpreter`.
- The REPL will store configuration in directory `.etil/repl` with the name `repl.json`.
- REPL configuration loading can be completely disabled with the OS command line option `--noconfig`.
- REPL Configuration file loading can be overridden when executing `etil_repl` in the OS command line by appending the 
"-c"/--"config" option followed by an absolute or relative file path to an explicit REPL JSON configuration file 
to be loaded.
- REPL Configuration file loading can also be overridden when executing `etil_repl` by defining the environment variable `ETIL_REPL_CONFIG`
which shall contain an absolute or relative file path to an explicit REPL JSON configuration file to be loaded.
- REPL configuration files are searched for in the following order and the first one found is loaded:
  1. The OS command line by appending the "-c"/--"config" option.
  2. The `ETIL_REPL_CONFIG` environment variable.
  3. The `./.etil/repl/repl.json` file in the current working directory.
  4. The `${HOME}/.etil/repl/repl.json` file in the users home directory.

## REPL User Experience Enhancements

- General Goals:I want to enhance the REPL user experience...
  - To make it more attractive.
  - Easier for the user to visually parse the REPL interactions.
  - Minimize the amount of typing with command line typing with history. 
  

### Color:
#### I want...
 - Color using ANSI ESC sequences that are compatible with both linux and Windows command line terminals. 
 - Two color themes switchable in the REPL: `/LIGHT` and `/DARK`.
 - The input and output in different colors based on the theme.
 - The interpreter errors in a third color defaulting to red.
 - I want the theme colors stored in the REPL configuration file and loaded on REPL startup.

### Command Line Editor & History:
**Note:** This Command Line Editor is specific to the REPL command line when running the REPL interactively, 
not the OS command line. 

#### I want...
- To be able to `up arrow`/`down arrow` through the last `max-history-lines` historical REPL line entries.
- To be able to edit the selected/entered REPL line.
- To execute the line when `Enter` is pressed regardless of where the cursor is on the line when editing.
-  The configuration parameter for the maximum number of REPL lines `max-history-lines` shall be stored 
in the REPL configuration file and loaded on REPL startup.
- The actual storage of the REPL history shall be persisted in the configuration file `.etil/repl/history.json`. 
- The history file shall be updated every time the `Enter` key is pressed but before the line is sent to the interpreter.
  - Make sure to close the `.etil/repl/history.json` file immediately after it read or updated.
- Add a REPL command `/history` that prints the REPL command line history to the console. 
