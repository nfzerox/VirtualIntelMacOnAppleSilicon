-- Create ONLY an owned comparison window; never change an existing window or
-- global Finder defaults. Use the same twelve-image folder on both machines.
on run arguments
    if (count arguments) is not 1 then error "Supply the owned comparison folder."
    set folderPath to item 1 of arguments
    tell application "Finder"
        set folderAlias to POSIX file folderPath as alias
        if not (exists file "Blue Pond.jpg" of folderAlias) then error "Comparison image is missing."
        set w to make new Finder window to folderAlias
        set current view of w to flow view
        set toolbar visible of w to false
        set statusbar visible of w to false
        set bounds of w to {180, 150, 1000, 720}
        activate
        -- Finder loads the directory asynchronously and may replace the first
        -- selection with its default row. Establish stable metadata outside
        -- the measured workload; visual confirmation is still separate.
        set stableReads to 0
        repeat with attempt from 1 to 50
            if (count selection) is 1 then
                if name of item 1 of selection is "Blue Pond.jpg" then
                    set stableReads to stableReads + 1
                else
                    set stableReads to 0
                end if
            else
                set stableReads to 0
            end if
            if stableReads is 3 then return id of w
            if stableReads is 0 then set selection to file "Blue Pond.jpg" of target of w
            delay 0.1
        end repeat
        close w
        error "Finder did not establish the requested comparison selection."
    end tell
end run
