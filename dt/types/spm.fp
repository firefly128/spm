#
# spm.fp - CDE Front Panel entry for Sunstorm Package Manager
#
# Adds spm-gui to the Applications drawer on the front panel (dock).
#
# Install to: /etc/dt/appconfig/types/C/spm.fp
# After install, restart the workspace manager or log out/in.
#

CONTROL SpmGui
{
  TYPE                  icon
  CONTAINER_NAME        PersAppsSubpanel
  CONTAINER_TYPE        SUBPANEL
  POSITION_HINTS        1
  ICON                  Spm
  LABEL                 Package Manager
  PUSH_ACTION           SpmGui
}
