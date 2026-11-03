-- Finite real Finder workload, identical on native/guest Mavericks.
-- Usage: osascript THIS_FILE WINDOW_ID EXPECTED_FOLDER
-- Prepare a dedicated window and image folder first. Finder's reported sort
-- column can differ from its visible sort: verify the actual Name header/order.
-- This tests visible correctness, not FPS. No AX, synthetic window or GL calls.
on run arguments
    if (count arguments) is not 2 then error "Supply an owned Finder window ID and its expected folder."
    set windowID to item 1 of arguments as integer
    set folderPath to item 2 of arguments
    tell application "Finder"
        set w to Finder window id windowID
        if (POSIX path of (target of w as alias)) is not (POSIX path of (POSIX file folderPath as alias)) then error "Finder window target changed; no workload was posted."
        if current view of w is not flow view then error "Prepare Cover Flow before running."
        repeat with filename in {"Blue Pond.jpg", "Beach.jpg", "Flamingos.jpg"}
            if not (exists file (contents of filename) of target of w) then error "The comparison folder is incomplete."
        end repeat
        set originalBounds to bounds of w
        set originalSelection to selection
        activate
        try
            repeat with cycle from 1 to 2
                repeat with filename in {"Blue Pond.jpg", "Beach.jpg", "Flamingos.jpg"}
                    set selection to file (contents of filename) of target of w
                    set bounds of w to {180, 150, 1000, 720}
                    log "cycle " & cycle & " wide " & filename
                    delay 0.5
                    set bounds of w to {220, 180, 1020, 700}
                    log "cycle " & cycle & " narrow " & filename
                    delay 0.5
                end repeat
            end repeat
        on error messageText number errorNumber
            set bounds of w to originalBounds
            set selection to originalSelection
            error messageText number errorNumber
        end try
        set bounds of w to originalBounds
        set selection to originalSelection
        return "Completed two real Finder selection/resize cycles; original bounds and selection restored."
    end tell
end run
