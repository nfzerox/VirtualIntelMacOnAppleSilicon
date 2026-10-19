use framework "Foundation"
use scripting additions

on clockSeconds()
    return (current application's NSDate's timeIntervalSinceReferenceDate()) as real
end clockSeconds

-- Non-destructive menu opens only; never selects a menu command.
tell application "Calculator" to activate
delay 1
repeat with trial from 1 to 3
    repeat with menuName in {"File", "Edit", "View"}
        set queryStarted to my clockSeconds()
        tell application "System Events" to tell process "Calculator" to get name of menu bar item (contents of menuName) of menu bar 1
        set queryElapsed to (my clockSeconds()) - queryStarted
        set started to my clockSeconds()
        tell application "System Events"
            tell process "Calculator"
                click menu bar item (contents of menuName) of menu bar 1
                set clickedAt to my clockSeconds()
                set itemCount to count of menu items of menu 1 of menu bar item (contents of menuName) of menu bar 1
            end tell
        end tell
        set elapsed to (my clockSeconds()) - started
        log "trial=" & trial & " menu=" & (contents of menuName) & " response_seconds=" & elapsed & " query_seconds=" & queryElapsed & " click_seconds=" & (clickedAt - started) & " items=" & itemCount
        tell application "System Events" to key code 53
        delay 0.2
    end repeat
end repeat
