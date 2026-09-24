if (!A_IsAdmin)
{
    try
    {
        if A_IsCompiled
            Run *RunAs "%A_ScriptFullPath%"
        else
            Run *RunAs "%A_AhkPath%" "%A_ScriptFullPath%"
    }
    ExitApp
}
