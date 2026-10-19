-- Adds only our synthetic fixture to a clearly named project; never deletes
-- or rebuilds the user's existing image library or caches.
with timeout of 20 seconds
    set fixturePath to (POSIX path of (path to home folder)) & "pvg-aperture-probes/chart.png"
    tell application "Aperture"
        tell library 1
            if not (exists project "PVG Diagnostic") then
                set destinationProject to make new project with properties {name:"PVG Diagnostic"}
            else
                set destinationProject to project "PVG Diagnostic"
            end if
        end tell
        set importedVersions to import {POSIX file fixturePath} by referencing into destinationProject
        reveal item 1 of importedVersions
        activate
    end tell
end timeout
