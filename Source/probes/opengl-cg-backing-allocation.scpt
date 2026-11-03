-- Real Finder windows only. Never resize/close an existing user window.
-- Run in the logged-in user's context; no System Events/accessibility input.
set createdWindows to {}
try
    tell application "Finder"
        repeat with n from 0 to 2
            set newWindow to make new Finder window to folder "Applications" of startup disk
            set end of createdWindows to id of newWindow
            set bounds of newWindow to {100 + n * 30, 100 + n * 30, 900 + n * 60, 650 + n * 50}
        end repeat
        activate
    end tell
    delay 3
on error errorText number errorNumber
    tell application "Finder"
        repeat with windowID in createdWindows
            try
                close Finder window id (windowID as integer)
            end try
        end repeat
    end tell
    error errorText number errorNumber
end try
tell application "Finder"
    repeat with windowID in createdWindows
        try
            close Finder window id (windowID as integer)
        end try
    end repeat
end tell
delay 1
return "Completed real Finder allocation workload; closed only its three new windows."
