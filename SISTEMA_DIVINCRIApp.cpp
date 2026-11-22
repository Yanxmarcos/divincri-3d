#include "SISTEMA_DIVINCRIApp.h"
#include "SISTEMA_DIVINCRIMain.h"
#include <wx/filename.h>
#include <wx/stdpaths.h>

IMPLEMENT_APP(SISTEMA_DIVINCRIApp);

bool SISTEMA_DIVINCRIApp::OnInit()
{
    const wxFileName executable(wxStandardPaths::Get().GetExecutablePath());
    wxFileName assets(executable.GetPath(), "");
    assets.AppendDir("assets");
    if (assets.DirExists()) wxSetWorkingDirectory(executable.GetPath());

    wxInitAllImageHandlers();
    auto* frame = new SISTEMA_DIVINCRIFrame(nullptr, "DIVINCRI 3D | Editor de escenas forenses");
    SetTopWindow(frame);
    frame->Show();
    return true;
}
