-- Finite model-edit repaint comparison, NOT keyboard/HID-to-photon timing.
-- Run with osascript on a Mavericks reference or guest with Numbers 2.3.
-- Only an explicitly supplied disposable VIM-IWork-Input.numbers is accepted.
-- Use the real-key diagnostic separately when measuring interactive input.
on run argv
    if (count argv) is not 1 then error "Supply the disposable VIM-IWork-Input.numbers document."
    set documentPath to item 1 of argv
    set fixture to POSIX file documentPath as alias
    set fixtureName to name of (info for fixture)
    if fixtureName is not "VIM-IWork-Input.numbers" then error "Refusing to edit a non-fixture document."
    set expected to ""
    set began to current date
    tell application "/Applications/iWork '09/Numbers.app"
        activate
        open fixture
        set testDocument to missing value
        repeat with candidate in documents
            -- Unrelated unsaved documents have no path. Never select or edit
            -- one of those merely because it is the app's front document.
            try
                if (POSIX file (path of candidate) as alias) is fixture then set testDocument to contents of candidate
            end try
        end repeat
        if testDocument is missing value then error "The requested fixture did not open."
        tell table 1 of sheet 1 of testDocument
            set value of cell "B3" to ""
            repeat 48 times
                set expected to expected & "x"
                set value of cell "B3" to expected
                delay 0.1
            end repeat
            if (value of cell "B3" as text) is not expected then error "The fixture did not retain all edits."
        end tell
    end tell
    return "48 model edits verified; elapsed whole seconds=" & ((current date) - began) & ". This is a repaint comparison, not physical-key latency."
end run
