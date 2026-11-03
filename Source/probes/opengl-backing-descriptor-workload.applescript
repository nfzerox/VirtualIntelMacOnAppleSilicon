-- Create only our own real Finder windows; keep them alive for the VM-map
-- survey. Returned IDs permit exact cleanup without closing existing windows.
on run surveyArguments
if (count of surveyArguments) > 0 then
    tell application "Finder"
        repeat with surveyID in surveyArguments
            try
                close (Finder window id (surveyID as integer))
            end try
        end repeat
    end tell
    return "Closed requested survey window IDs only"
end if
tell application "Finder"
    activate
    set surveyWindows to {}
    repeat with i from 1 to 3
        set surveyWindow to make new Finder window
        set target of surveyWindow to (POSIX file "/Applications" as alias)
        set current view of surveyWindow to icon view
        set end of surveyWindows to id of surveyWindow
        repeat with j from 1 to 4
            set bounds of surveyWindow to {120 + i * 35, 110 + i * 35, 720 + i * 50 + j * 12, 550 + j * 20}
            delay 0.12
        end repeat
    end repeat
    return surveyWindows
end tell
end run
