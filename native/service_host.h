#pragma once
// "DNFAutoFire.exe --service": the Windows service entry of the unified EXE.
// Runs as LocalSystem in session 0 with no window; all work happens on the service
// thread (game lifecycle, launcher monitor, config hot reload) and is logged to
// logs\service.log next to the EXE.

namespace dafclient::svc {

// Connects to the service control manager and blocks until the service stops.
// Returns a non-zero exit code when not started by the SCM.
int runService();

} // namespace dafclient::svc
