# TUI Server Response Formatting

### **GOAL**: Decode the server response for both the Server I/O window and the .log text log file,and display it in a more readable and appealing way.

- The server response in the is a JSON object with keys "error", "output", and "stack". (Ignore "stackStatus", we may deprecate it.)
- The unpacked server response will be displayed in the Server I/O window in place of the JSON response.

### Example response:
```json
 {"errors":"","output":"p1:(123 ,456 )\np2:(987 ,-654 )\np1:(123,456)\np2:(987,-654)\n","stack":[],"stackStatus":"(0)"}
```

## Response Formatting 

- The .log file will get the all the text in place of the JSON response as described below but not colored.

### The response should be unpacked/formatted/routed as follows:
- The "error" key should be displayed if non-zero length and in the Notification window in red.
- The "output" key should be displayed if non-zero length and in the Server I/O in green,
with all the carriage control decoded and honored.
- The "stack" key should be displayed on a new line and in the Server I/O in yellow.
- The "stack" key's value should be displayed as received from the server, with any carriage control still encoded in the JSON.

## Verification

1. Start TUI, type 42 dup + → Server I/O shows 84 in green (not JSON)
2. Type bad-word → error shown in red in Server I/O and notification bar
3. Type 42 → stack [42] shown in yellow
4. Type /log, run some commands, /log → text log shows clean output, not JSON
5. Type /verbose on, run a command → full JSON still shown (verbose mode unchanged)
6. Test /stats, /stack, /reset — these may return different formats, verify fallback works 