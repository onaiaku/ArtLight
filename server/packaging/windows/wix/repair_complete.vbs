' Displays a simple message after a successful Repair to inform the user
' that no reboot is required.

Function ShowRepairComplete()
    Dim msg
    msg = "ArtLight Server repair completed successfully. No reboot is required."
    MsgBox msg, vbInformation + vbOKOnly, "ArtLight Server Repair"
    ShowRepairComplete = 0
End Function

