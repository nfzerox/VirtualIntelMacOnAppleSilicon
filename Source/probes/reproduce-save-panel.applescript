-- Run inside the guest with Accessibility permission. Opens Terminal's export
-- panel if needed, toggles only its disclosure control, never saves a file.
tell application "System Events" to tell process "Terminal"
    set frontmost to true
    if not (exists sheet 1 of window 1) then
        click menu item "Export Text As…" of menu 1 of menu bar item "Shell" of menu bar 1
        repeat 30 times
            if exists sheet 1 of window 1 then exit repeat
            delay 0.2
        end repeat
    end if
    repeat 6 times
        click (first UI element of sheet 1 of window 1 whose role is "AXDisclosureTriangle")
        delay 0.7
    end repeat
end tell
