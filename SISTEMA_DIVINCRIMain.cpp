#include "SISTEMA_DIVINCRIMain.h"
#include <wx/artprov.h>
#include <wx/statbmp.h>
#include <wx/image.h>
#include <wx/bmpbuttn.h>
#include <wx/statline.h>
#include <wx/clntdata.h>
#include <wx/wupdlock.h>
#include <wx/tokenzr.h>
#include <wx/timer.h>
#include <wx/filedlg.h>
#include <wx/msgdlg.h>
#include <wx/filefn.h>


// Includes para 3D
#include <algorithm>
#include <fstream>
#include <sstream>
#include <cmath>

#ifdef __WXMAC__
#include <GLUT/glut.h>
#else
#include <GL/glu.h>
#include <GL/gl.h>
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ==================================================================
// 1. IMPLEMENTACIÓN DEL CANVAS 3D
// ==================================================================

BEGIN_EVENT_TABLE(MyGLCanvas, wxGLCanvas)
    EVT_PAINT(MyGLCanvas::OnPaint)
    EVT_SIZE(MyGLCanvas::OnResize)
    EVT_KEY_DOWN(MyGLCanvas::OnKeyDown)
    EVT_MOTION(MyGLCanvas::OnMouseMove)
    EVT_LEFT_DOWN(MyGLCanvas::OnMouseLeftClick)
END_EVENT_TABLE()

MyGLCanvas::MyGLCanvas(wxWindow* parent, const wxSize& size, wxString pathOBJ, wxString pathTex)
    : wxGLCanvas(parent, wxID_ANY, NULL, wxDefaultPosition, size, wxFULL_REPAINT_ON_RESIZE)
{
    m_context = new wxGLContext(this);
    m_initGL = false;

    // Estado inicial
    m_indiceSeleccionado = -1;
    m_modoTransformacion = 0;

    // Cámara Original
    m_camX = 0.0f; m_camY = 1.7f; m_camZ = 5.0f;
    m_camYaw = -90.0f; m_camPitch = -10.0f;
    m_isCameraActive = false;

    // Cargar ESCENA BASE
    m_pathBaseTex = pathTex;
    m_baseLoaded = false;
    m_baseTextureID = 0;
    CargarOBJGenerico(pathOBJ.ToStdString(), m_baseVertices, m_baseTexCoords, m_baseNormals, m_baseFaces);
    m_baseLoaded = !m_baseFaces.empty();
}

MyGLCanvas::~MyGLCanvas() { delete m_context; }

// --- CARGADORES GENÉRICOS ---
void MyGLCanvas::CargarOBJGenerico(const std::string& path, std::vector<Vertex>& v, std::vector<TexCoord>& vt, std::vector<Normal>& vn, std::vector<Face>& f) {
    std::ifstream file(path.c_str());
    if (!file.is_open()) return;

    v.clear(); vt.clear(); vn.clear(); f.clear();
    std::string line;
    while (std::getline(file, line)) {
        std::stringstream ss(line);
        std::string prefix;
        ss >> prefix;
        if (prefix == "v") {
            Vertex vertex;
            if (ss >> vertex.x >> vertex.y >> vertex.z) v.push_back(vertex);
        } else if (prefix == "vt") {
            TexCoord uv;
            if (ss >> uv.u >> uv.v) vt.push_back(uv);
        } else if (prefix == "vn") {
            Normal normal;
            if (ss >> normal.nx >> normal.ny >> normal.nz) vn.push_back(normal);
        } else if (prefix == "f") {
            // The renderer consumes triangles with explicit position, UV and normal.
            std::vector<Face> triangles;
            Face face{};
            std::string corner;
            int count = 0;
            bool valid = true;
            while (ss >> corner) {
                if (corner.front() == '#') break;
                std::istringstream indices(corner);
                int vertex, uv, normal;
                char firstSlash, secondSlash;
                if (!(indices >> vertex >> firstSlash >> uv >> secondSlash >> normal) ||
                    firstSlash != '/' || secondSlash != '/' ||
                    vertex <= 0 || static_cast<size_t>(vertex) > v.size() ||
                    uv <= 0 || static_cast<size_t>(uv) > vt.size() ||
                    normal <= 0 || static_cast<size_t>(normal) > vn.size()) {
                    valid = false;
                    break;
                }
                const int slot = count < 2 ? count : 2;
                face.vIndex[slot] = vertex - 1;
                face.tIndex[slot] = uv - 1;
                face.nIndex[slot] = normal - 1;
                if (++count >= 3) {
                    triangles.push_back(face);
                    face.vIndex[1] = face.vIndex[2];
                    face.tIndex[1] = face.tIndex[2];
                    face.nIndex[1] = face.nIndex[2];
                }
            }
            if (valid) f.insert(f.end(), triangles.begin(), triangles.end());
        }
    }
}

void MyGLCanvas::CargarTexturaGenerica(const wxString& path, GLuint& texID) {
    wxLogNull silenceImageWarnings;
    wxImage image;
    if (!image.LoadFile(path)) return;

    glGenTextures(1, &texID);
    glBindTexture(GL_TEXTURE_2D, texID);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    // wxImage holds RGB and alpha in separate buffers. The editor renders opaque geometry.
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, image.GetWidth(), image.GetHeight(),
                 0, GL_RGB, GL_UNSIGNED_BYTE, image.GetData());
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
}

// --- GESTIÓN DE OBJETOS EXTRA ---
bool MyGLCanvas::AgregarObjetoExtra(wxString nombre, wxString pathObj, wxString pathTex) {
    ObjetoExtra obj;
    obj.nombre = nombre.ToStdString();
    obj.visible = true;
    obj.textureID = 0;

    // Posición inicial: Frente a la cámara
    float radYaw = m_camYaw * (M_PI / 180.0f);
    obj.posX = m_camX + cos(radYaw) * 3.0f;
    obj.posY = pathObj.StartsWith("assets/demo/") ? 0.0f : 0.5f; // Un poco elevado del suelo
    obj.posZ = m_camZ + sin(radYaw) * 3.0f;

    obj.rotX = 0; obj.rotY = 0; obj.rotZ = 0;
    obj.scaleX = 1; obj.scaleY = 1; obj.scaleZ = 1;

    CargarOBJGenerico(pathObj.ToStdString(), obj.vertices, obj.texCoords, obj.normals, obj.faces);

    if (obj.faces.empty() || !SetCurrent(*m_context)) return false;
    CargarTexturaGenerica(pathTex, obj.textureID);

    m_objetosExtra.push_back(obj);
    // Al agregar, seleccionamos automáticamente para editar
    m_indiceSeleccionado = m_objetosExtra.size() - 1;
    Refresh();
    return true;
}

void MyGLCanvas::SeleccionarObjeto(int index) {
    m_indiceSeleccionado = index;
    Refresh();
}

void MyGLCanvas::BorrarObjeto(int index) {
    if (index >= 0 && index < (int)m_objetosExtra.size()) {
        if (SetCurrent(*m_context)) glDeleteTextures(1, &m_objetosExtra[index].textureID);
    m_objetosExtra.erase(m_objetosExtra.begin() + index);
        m_indiceSeleccionado = -1;
        Refresh();
    }
}

void MyGLCanvas::SetVisibilidad(int index, bool visible) {
    if (index >= 0 && index < (int)m_objetosExtra.size()) {
        m_objetosExtra[index].visible = visible;
        Refresh();
    }
}

void MyGLCanvas::SetModoTransformacion(int modo) {
    m_modoTransformacion = modo;
    Refresh();
}

// --- EVENTOS ---
void MyGLCanvas::OnResize(wxSizeEvent& event) { Refresh(); event.Skip(); }
void MyGLCanvas::OnPaint(wxPaintEvent& WXUNUSED(event)) { wxPaintDC dc(this); Render(); }

void MyGLCanvas::OnMouseLeftClick(wxMouseEvent& event) {
    // Activa la cámara al hacer click
    if (!m_isCameraActive) {
        m_isCameraActive = true;
        m_lastMousePos = event.GetPosition();
        SetCursor(wxCURSOR_BLANK);
        SetFocus();
    }
}

void MyGLCanvas::OnMouseMove(wxMouseEvent& event) {
    // MOVIMIENTO DE CÁMARA (MOUSE LOOK)
    if (m_isCameraActive) {
        wxPoint currentPos = event.GetPosition();
        int dx = currentPos.x - m_lastMousePos.x;
        int dy = currentPos.y - m_lastMousePos.y;

        float sensitivity = 0.15f;
        m_camYaw += dx * sensitivity;
        m_camPitch -= dy * sensitivity;

        if (m_camPitch > 89.0f) m_camPitch = 89.0f;
        if (m_camPitch < -89.0f) m_camPitch = -89.0f;

        int w, h; GetClientSize(&w, &h);
        WarpPointer(w / 2, h / 2);
        m_lastMousePos = wxPoint(w / 2, h / 2);
        Refresh();
    }
}

// --- LÓGICA DE TECLAS ACTUALIZADA ---
void MyGLCanvas::OnKeyDown(wxKeyEvent& event) {
    int key = event.GetKeyCode();

    // 1. TECLA ESCAPE: SOLO LIBERA EL MOUSE
    if (key == WXK_ESCAPE) {
        m_isCameraActive = false;
        SetCursor(wxCURSOR_ARROW);
        Refresh();
        return;
    }

    // 2. TECLA ESPACIO: TERMINAR EDICIÓN DE OBJETO
    if (key == WXK_SPACE) {
        if (m_indiceSeleccionado != -1) {
            m_indiceSeleccionado = -1;
        }
        Refresh();
        return;
    }

    if (!m_isCameraActive) return;

    // 3. MODO EDICIÓN DE OBJETO
    if (m_indiceSeleccionado != -1) {
        ObjetoExtra& obj = m_objetosExtra[m_indiceSeleccionado];
        float moveSpeed = 0.1f;
        float rotSpeed = 3.0f;
        float scaleSpeed = 0.05f;

        // --- TRASLACIÓN ---
        if (key == 'O' || key == 'o') obj.posY += moveSpeed; // Arriba
        if (key == 'I' || key == 'i') obj.posY -= moveSpeed; // Abajo
        if (key == 'L' || key == 'l') obj.posX += moveSpeed; // Derecha
        if (key == 'K' || key == 'k') obj.posX -= moveSpeed; // Izquierda

        // Flechas para profundidad (Z)
        if (key == WXK_UP)   obj.posZ -= moveSpeed;
        if (key == WXK_DOWN) obj.posZ += moveSpeed;

        // --- NUEVO: M y N para profundidad también ---
        if (key == 'M' || key == 'm') obj.posZ -= moveSpeed; // Adelante (Fondo)
        if (key == 'N' || key == 'n') obj.posZ += moveSpeed; // Atras (Frente)

        // --- ROTACIÓN ---
        if (key == 'U' || key == 'u') obj.rotY -= rotSpeed; // Derecha
        if (key == 'Y' || key == 'y') obj.rotY += rotSpeed; // Izquierda

        // --- ESCALADO ---
        if (key == 'J' || key == 'j') {
            obj.scaleX += scaleSpeed; obj.scaleY += scaleSpeed; obj.scaleZ += scaleSpeed;
        }
        if (key == 'H' || key == 'h') {
            obj.scaleX -= scaleSpeed; obj.scaleY -= scaleSpeed; obj.scaleZ -= scaleSpeed;
            if (obj.scaleX < 0.1f) { obj.scaleX=0.1f; obj.scaleY=0.1f; obj.scaleZ=0.1f; }
        }
    }

    // 4. MODO CÁMARA LIBRE (WASD) - SIEMPRE ACTIVO
    float speed = 0.2f;
    float radYaw = m_camYaw * (M_PI / 180.0f);
    float frontX = cos(radYaw);
    float frontZ = sin(radYaw);
    float rightX = -sin(radYaw);
    float rightZ = cos(radYaw);

    if (key == 'W' || key == 'w') { m_camX += frontX * speed; m_camZ += frontZ * speed; }
    if (key == 'S' || key == 's') { m_camX -= frontX * speed; m_camZ -= frontZ * speed; }
    if (key == 'A' || key == 'a') { m_camX -= rightX * speed; m_camZ -= rightZ * speed; }
    if (key == 'D' || key == 'd') { m_camX += rightX * speed; m_camZ += rightZ * speed; }

    Refresh();
}

void MyGLCanvas::Render(bool present) {
    if (!IsShownOnScreen() || !SetCurrent(*m_context)) return;

    if (!m_initGL) {
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_TEXTURE_2D);
        CargarTexturaGenerica(m_pathBaseTex, m_baseTextureID);
        m_initGL = true;
    }

    glClearColor(0.53f, 0.81f, 0.92f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    int w, h; GetClientSize(&w, &h);
    if (w <= 0 || h <= 0) return;
    glViewport(0, 0, w, h);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluPerspective(60.0f, (float)w/h, 0.1f, 100.0f);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    float radYaw = m_camYaw * (M_PI / 180.0f);
    float radPitch = m_camPitch * (M_PI / 180.0f);
    gluLookAt(m_camX, m_camY, m_camZ,
              m_camX + cos(radPitch) * cos(radYaw),
              m_camY + sin(radPitch),
              m_camZ + cos(radPitch) * sin(radYaw),
              0.0f, 1.0f, 0.0f);

    // 1. DIBUJAR ESCENA BASE
    glPushMatrix();
    if (m_baseTextureID) glEnable(GL_TEXTURE_2D);
    else glDisable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, m_baseTextureID);
    glColor3f(1.0f, 1.0f, 1.0f);
    if (m_baseLoaded) {
        glBegin(GL_TRIANGLES);
        for (const auto& f : m_baseFaces) {
            for (int i=0; i<3; i++) {
                glNormal3f(m_baseNormals[f.nIndex[i]].nx, m_baseNormals[f.nIndex[i]].ny, m_baseNormals[f.nIndex[i]].nz);
                glTexCoord2f(m_baseTexCoords[f.tIndex[i]].u, m_baseTexCoords[f.tIndex[i]].v);
                glVertex3f(m_baseVertices[f.vIndex[i]].x, m_baseVertices[f.vIndex[i]].y, m_baseVertices[f.vIndex[i]].z);
            }
        }
        glEnd();
    }
    glPopMatrix();

    // 2. DIBUJAR OBJETOS EXTRA
    for (int i=0; i < (int)m_objetosExtra.size(); i++) {
        if (!m_objetosExtra[i].visible) continue;

        glPushMatrix();
        glTranslatef(m_objetosExtra[i].posX, m_objetosExtra[i].posY, m_objetosExtra[i].posZ);
        glRotatef(m_objetosExtra[i].rotY, 0.0f, 1.0f, 0.0f);
        glRotatef(m_objetosExtra[i].rotX, 1.0f, 0.0f, 0.0f);
        glRotatef(m_objetosExtra[i].rotZ, 0.0f, 0.0f, 1.0f);
        glScalef(m_objetosExtra[i].scaleX, m_objetosExtra[i].scaleY, m_objetosExtra[i].scaleZ);

        if (m_objetosExtra[i].textureID) glEnable(GL_TEXTURE_2D);
        else glDisable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, m_objetosExtra[i].textureID);

        if (i == m_indiceSeleccionado) glColor3f(1.0f, 0.5f, 0.5f);
        else glColor3f(1.0f, 1.0f, 1.0f);

        glBegin(GL_TRIANGLES);
        for (const auto& f : m_objetosExtra[i].faces) {
            for (int k=0; k<3; k++) {
                glNormal3f(m_objetosExtra[i].normals[f.nIndex[k]].nx, m_objetosExtra[i].normals[f.nIndex[k]].ny, m_objetosExtra[i].normals[f.nIndex[k]].nz);
                glTexCoord2f(m_objetosExtra[i].texCoords[f.tIndex[k]].u, m_objetosExtra[i].texCoords[f.tIndex[k]].v);
                glVertex3f(m_objetosExtra[i].vertices[f.vIndex[k]].x, m_objetosExtra[i].vertices[f.vIndex[k]].y, m_objetosExtra[i].vertices[f.vIndex[k]].z);
            }
        }
        glEnd();
        glPopMatrix();
    }

    glFlush();
    if (present) SwapBuffers();
}


// ==================================================================
// NUEVAS FUNCIONES PARA MYGLCANVAS
// ==================================================================

wxString MyGLCanvas::GetCoordenadasCamara() const {
    return wxString::Format("X: %.1f  Y: %.1f  Z: %.1f", m_camX, m_camY, m_camZ); //AQUI CAMBIE  m_camY, m_camZ
}

bool MyGLCanvas::ExportarCaptura(const wxString& filePath) {
    if(!IsShownOnScreen()) {
        wxLogError("Canvas no visible");
        return false;
    }

    if (!SetCurrent(*m_context)) return false;

    int w, h;
    GetClientSize(&w, &h);

    if (w <= 0 || h <= 0) {
        wxLogError(L"Tamaño del viewport inválido: %dx%d", w, h);
        return false;
    }

    try {
        // --- CORRECCIÓN IMPORTANTE ---
        // Ajustamos la alineación de empaquetado a 1 byte.
        // Esto evita que OpenGL escriba bytes de relleno extra si el ancho
        // de la ventana no es múltiplo de 4, evitando el crash.
        Render(false);
        glReadBuffer(GL_BACK);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        // -----------------------------

        // Crear buffer para almacenar los píxeles
        std::vector<unsigned char> pixels(w * h * 3);

        // Leer los píxeles del framebuffer
        glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());

        // Restaurar la alineación por defecto (buena práctica)
        glPixelStorei(GL_PACK_ALIGNMENT, 4);

        // Verificar errores de OpenGL
        GLenum error = glGetError();
        if (error != GL_NO_ERROR) {
            wxLogError(L"Error de OpenGL al leer píxeles: %d", error);
            return false;
        }

        // Crear imagen wxImage
        wxImage image(w, h);
        if (!image.IsOk()) {
            wxLogError("No se pudo crear la imagen wxImage");
            return false;
        }

        // Copiar y voltear los píxeles verticalmente (OpenGL tiene el origen abajo-izquierda)
        unsigned char* data = image.GetData();
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                // Fila invertida para origen OpenGL -> wxImage
                int srcIndex = ((h - 1 - y) * w + x) * 3;
                int dstIndex = (y * w + x) * 3;

                data[dstIndex]     = pixels[srcIndex];     // R
                data[dstIndex + 1] = pixels[srcIndex + 1]; // G
                data[dstIndex + 2] = pixels[srcIndex + 2]; // B
            }
        }

        // Guardar la imagen
        return image.SaveFile(filePath, wxBITMAP_TYPE_PNG);

    } catch (const std::exception& e) {
        wxLogError(L"Excepción al exportar captura: %s", e.what());
        return false;
    }
}

// ==================================================================
// 2. DIÁLOGO DE SELECCIÓN DE MODELOS
// ==================================================================

DialogoSeleccionModelos::DialogoSeleccionModelos(wxWindow* parent)
    : wxDialog(parent, wxID_ANY, L"Catálogo de Selección de Evidencias (OBJETOS 3D)",
               wxDefaultPosition, wxSize(900, 650), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
{
    wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);

    // TÍTULO
    mainSizer->Add(new wxStaticText(this, wxID_ANY,
        L"Seleccione un objeto para añadir a la escena:"),
        0, wxALL | wxALIGN_CENTER, 10);

    // ========== BARRA DE BÚSQUEDA Y CONTADOR ==========
    wxBoxSizer* searchSizer = new wxBoxSizer(wxHORIZONTAL);

    searchSizer->Add(new wxStaticText(this, wxID_ANY, "Buscar:"),
        0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);

    m_searchBox = new wxTextCtrl(this, wxID_ANY, "",
        wxDefaultPosition, wxSize(300, -1));
    m_searchBox->SetHint("Escriba para filtrar modelos...");
    m_searchBox->Bind(wxEVT_TEXT, &DialogoSeleccionModelos::OnBusqueda, this);
    searchSizer->Add(m_searchBox, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 15);

    m_contadorModelos = new wxStaticText(this, wxID_ANY, "");
    wxFont fontContador = m_contadorModelos->GetFont();
    fontContador.SetWeight(wxFONTWEIGHT_BOLD);
    m_contadorModelos->SetFont(fontContador);
    searchSizer->Add(m_contadorModelos, 0, wxALIGN_CENTER_VERTICAL);

    mainSizer->Add(searchSizer, 0, wxALL | wxALIGN_CENTER, 10);
    // ==================================================

    // ÁREA DE SCROLL CON LOS MODELOS
    m_scrollWindow = new wxScrolledWindow(this, wxID_ANY);
    m_scrollWindow->SetScrollbars(10, 10, 50, 50);
    wxBoxSizer* scrollSizer = new wxBoxSizer(wxVERTICAL);

    // ========== LLENAR CATÁLOGO DE MODELOS ==========
    m_todosLosModelos.push_back({"Marcador 01", "demo", "marcador.obj", "marcador.png", "pre-marcador.png"});
    m_todosLosModelos.push_back({"Caja de evidencia", "demo", "caja.obj", "caja.png", "pre-caja.png"});
    m_todosLosModelos.push_back({L"Cono de señalización", "demo", "cono.obj", "cono.png", "pre-cono.png"});
    m_todosLosModelos.push_back({"Mesa auxiliar", "demo", "mesa.obj", "mesa.png", "pre-mesa.png"});
    m_todosLosModelos.push_back({L"Maletín", "demo", "maletin.obj", "maletin.png", "pre-maletin.png"});
    m_todosLosModelos.push_back({"Valla de seguridad", "demo", "valla.obj", "valla.png", "pre-valla.png"});

    // Optional catalog from the original academic installation.
    // CARPETA JIM "J"
    m_todosLosModelos.push_back({ "Marcador V1", "J", "M1-EXTRA/obj-marker.obj", "M1-EXTRA/text-obj-marker.png", "M1-EXTRA/pre-marker.png" });
    m_todosLosModelos.push_back({ "Cuerpo con Bolsa V1", "J", "M2-EXTRA/obj-cuerpo.obj", "M2-EXTRA/text-obj-cuerpo.png", "M2-EXTRA/pre-cuerpo.png" });
    m_todosLosModelos.push_back({ "Botella Vidrio Verde Rota V1", "J", "1/broken_wine_bottle.obj", "1/textura1.png", "1/Screenshot_2.png" });
    m_todosLosModelos.push_back({ "Bala con Sangre V1", "J", "2/bala_sangrienta.obj", "2/text-bala.png", "2/pre-bala.png" });
    m_todosLosModelos.push_back({ "Vaso Sucio V1", "J", "3/vaso_sucio.obj", "3/text-vaso.png", "3/pre-vaso.png" });
    m_todosLosModelos.push_back({ "Sacos Sucios V1", "J", "4/sacos.obj", "4/text-sacos.png", "4/pre-sacos.png" });
    m_todosLosModelos.push_back({ L"Televisión V1", "J", "5/television.obj", "5/text-tele.png", "5/pre-tele.png" });
    m_todosLosModelos.push_back({ "Mesa V1", "J", "6/mesa.obj", "6/text-mesa.png", "6/pre-mesa.png" });
    m_todosLosModelos.push_back({ "Cabeza de Calavera V1", "J", "7/calavera.obj", "7/text-calavera.png", "7/pre-calavera.png" });
    m_todosLosModelos.push_back({ L"Lámpara V1", "J", "8/lampara.obj", "8/text-lampara.png", "8/pre-lampara.png" });
    m_todosLosModelos.push_back({ "Libreros V1", "J", "9/libreros.obj", "9/text-libreros.png", "9/pre-libreros.png" });
    m_todosLosModelos.push_back({ L"Bolsa de Cartón V1", "J", "10/bolsaCarton.obj", "10/text-bolsa.png", "10/pre-bolsa.png" });
    m_todosLosModelos.push_back({ L"Teléfono de Casa V1", "J", "11/telefono.obj", "11/text-phono.png", "11/pre-phono.png" });
    m_todosLosModelos.push_back({ "Maceta con Planta V1", "J", "12/maceta.obj", "12/text-maceta.png", "12/pre-maceta.png" });
    m_todosLosModelos.push_back({ "Puerta V1", "J", "13/puerta.obj", "13/text-puerta.png", "13/pre-puerta.png" });
    m_todosLosModelos.push_back({ "Refrigeradora V1", "J", "14/refri.obj", "14/text-refri.png", "14/pre-refri.png" });
    m_todosLosModelos.push_back({ "Cocina V1", "J", "15/cocina.obj", "15/text-cocina.png", "15/pre-cocina.png" });

    // CARPETA CAZORLA "C"
    m_todosLosModelos.push_back({ "Auto Chocado N1", "C", "M1/auto.obj", "M1/text-auto.png", "M1/pre-auto.png" });
    m_todosLosModelos.push_back({ "Bolsa Evidencia N1", "C", "M2/bolsa_evidencia.obj", "M2/text-bolsa_evidencia.jpeg", "M2/pre-bolsa_evidencia.png" });
    m_todosLosModelos.push_back({ "Cuchillo con Sangre N1", "C", "M3/cuchillo_sangre.obj", "M3/text-cuchillo_sangre.png", "M3/pre-cuchillo.png" });
    m_todosLosModelos.push_back({ L"Cámara N1", "C", "M4/camara.obj", "M4/text-camara.png", "M4/pre-camara.png" });
    m_todosLosModelos.push_back({ "Cuerpo con Bolsa N2", "C", "M5/cuerpo.obj", "M5/text-cuerpo.png", "M5/pre-cuerpo.png" });
    m_todosLosModelos.push_back({ "Cuerpo con Bolsa N3", "C", "M6/cuerpo_sangre.obj", "M6/text-cuerpo_sangre.png", "M6/pre-cuerpo_sangre.png" });
    m_todosLosModelos.push_back({ L"Restricción Policial N1", "C", "M7/restriccion_policial.obj", "M7/text-restriccion_policial.png", "M7/pre-restriccion_policial.png" });
    m_todosLosModelos.push_back({ L"Cinta Métrica N1", "C", "M8/cinta_metrica.obj", "M8/text-cinta_metrica.png", "M8/pre-cinta_metrica.png" });
    m_todosLosModelos.push_back({ "Hacha con Sangre N1", "C", "M9/hacha_sangre.obj", "M9/text-hacha_sangre.png", "M9/pre-hacha_sangre.png" });
    m_todosLosModelos.push_back({ "Ambulancia N1", "C", "M10/ambulancia.obj", "M10/text-ambulancia.png", "M10/pre-ambulancia.png" });
    m_todosLosModelos.push_back({ "Tacho de Basura N1", "C", "M11/tacho_basura.obj", "M11/text-tacho_basura.png", "M11/pre-tacho_basura.png" });
    m_todosLosModelos.push_back({ L"Señal de Stop N1", "C", "M12/senial_stop.obj", "M12/text-senial_stop.png", "M12/pre-senial_stop.png" });
    m_todosLosModelos.push_back({ L"Semáforo N1", "C", "M13/semaforo.obj", "M13/text-semaforo.png", "M13/pre-semaforo.png" });
    m_todosLosModelos.push_back({ "Banco de Calle N1", "C", "M14/banca.obj", "M14/text-banca.png", "M14/pre-banca.png" });
    m_todosLosModelos.push_back({ "Balas N1", "C", "M15/balas.obj", "M15/text-balas.png", "M15/pre-balas.png" });

    // CARPETA ELIASER "E"
    m_todosLosModelos.push_back({ "Camiseta R1", "E", "1/obj-camisa_jean.obj", "1/text-camisa_jean.png", "1/pre-camisa_jean.png" });
    m_todosLosModelos.push_back({ "Cono R1", "E", "2/cono.obj", "2/text-cono.png", "2/pre-cono.png" });
    m_todosLosModelos.push_back({ "Cuchillo R1", "E", "3/cuchillo.obj", "3/text-cuchillo.png", "3/pre-cuchillo.png" });
    m_todosLosModelos.push_back({ "Estante R1", "E", "4/estante.obj", "4/text-estante.png", "4/pre-estante.png" });
    m_todosLosModelos.push_back({ "Libro R1", "E", "5/libro.obj", "5/text-libro.png", "5/pre-libro.png" });
    m_todosLosModelos.push_back({ "Libro R2", "E", "6/libro.obj", "6/text-libro.png", "6/pre-libro.png" });
    m_todosLosModelos.push_back({ "Mesa de Madera R1", "E", "7/mesa.obj", "7/text-mesa.png", "7/pre-mesa.png" });
    m_todosLosModelos.push_back({ "Mesa Redonda R1", "E", "8/mesa.obj", "8/text-mesa.png", "8/pre-mesa.png" });
    m_todosLosModelos.push_back({ "Pistola R1", "E", "9/pistola.obj", "9/text-pistola.png", "9/pre-pistola.png" });
    m_todosLosModelos.push_back({ "Revolver R1", "E", "10/revolver.obj", "10/text-revolver.png", "10/pre-revolver.png" });
    m_todosLosModelos.push_back({ "Silla de Madera R1", "E", "11/silla.obj", "11/text-silla.png", "11/pre-silla.png" });
    m_todosLosModelos.push_back({ "Gaseosa Enlatada Sprite R1", "E", "12/soda.obj", "12/text-soda.png", "12/pre-soda.png" });
    m_todosLosModelos.push_back({ "Tijera R1", "E", "13/tijera.obj", "13/text-tijera.png", "13/pre-tijera.png" });
    m_todosLosModelos.push_back({ "Zapatillas de Hombre R1", "E", "14/zapatos.obj", "14/text-zapatos.png", "14/pre-zapatos.png" });
    m_todosLosModelos.push_back({ "Zapatos de Taco de Mujer R1", "E", "15/zapatos_mujer.obj", "15/text-zapatos_mujer.png", "15/pre-zapatos_mujer.png" });

    const auto missingFiles = [](const ItemCatalogo& item) {
        const wxString folder = "assets/" + item.carpeta + "/";
        return !wxFileExists(folder + item.obj) ||
               !wxFileExists(folder + item.tex) ||
               !wxFileExists(folder + item.img);
    };
    m_todosLosModelos.erase(std::remove_if(m_todosLosModelos.begin(), m_todosLosModelos.end(),
                                          missingFiles), m_todosLosModelos.end());

    // Grid para las tarjetas
    m_gridSizer = new wxGridSizer(3, 20, 20);
    scrollSizer->Add(m_gridSizer, 1, wxALL | wxALIGN_CENTER_HORIZONTAL, 20);

    m_scrollWindow->SetSizer(scrollSizer);
    mainSizer->Add(m_scrollWindow, 1, wxEXPAND);

    // Botón Cancelar
    wxButton* btnCancel = new wxButton(this, wxID_CANCEL, "Cancelar");
    mainSizer->Add(btnCancel, 0, wxALL | wxALIGN_RIGHT, 10);

    SetSizer(mainSizer);

    // Mostrar todos los modelos inicialmente
    ActualizarVista("");
}

// ========== FUNCIÓN DE BÚSQUEDA ==========
void DialogoSeleccionModelos::OnBusqueda(wxCommandEvent& event) {
    wxString filtro = m_searchBox->GetValue();
    ActualizarVista(filtro);
}

// ========== ACTUALIZAR VISTA CON FILTRO ==========
void DialogoSeleccionModelos::ActualizarVista(const wxString& filtro) {
    // Limpiar el grid actual
    m_gridSizer->Clear(true);  // true = destruir los elementos

    wxString filtroLower = filtro.Lower();
    int modelosMostrados = 0;

    // Iterar sobre todos los modelos
    for (const auto& it : m_todosLosModelos) {
        // Filtrar por nombre (búsqueda case-insensitive)
        if (!filtro.IsEmpty()) {
            wxString nombreLower = it.nombre.Lower();
            if (nombreLower.Find(filtroLower) == wxNOT_FOUND) {
                continue;  // No coincide, saltar este modelo
            }
        }

        modelosMostrados++;

        // Crear tarjeta para este modelo
        wxPanel* card = new wxPanel(m_scrollWindow, wxID_ANY, wxDefaultPosition,
                                    wxDefaultSize, wxBORDER_SIMPLE);
        wxBoxSizer* cs = new wxBoxSizer(wxVERTICAL);
        card->SetBackgroundColour(*wxWHITE);

        // Cargar imagen de preview
        wxString rutaImg = "assets/" + it.carpeta + "/" + it.img;
        wxImage img;
        if(img.LoadFile(rutaImg, wxBITMAP_TYPE_PNG)) {
            img.Rescale(160, 120);
            cs->Add(new wxStaticBitmap(card, wxID_ANY, wxBitmap(img)),
                   0, wxALL | wxALIGN_CENTER, 10);
        } else {
            wxPanel* ph = new wxPanel(card, wxID_ANY, wxDefaultPosition, wxSize(160, 120));
            ph->SetBackgroundColour(wxColour(220, 220, 220));
            cs->Add(ph, 0, wxALL | wxALIGN_CENTER, 10);
        }

        // Nombre del modelo
        wxStaticText* txt = new wxStaticText(card, wxID_ANY, it.nombre);
        wxFont f = txt->GetFont();
        f.SetWeight(wxFONTWEIGHT_BOLD);
        txt->SetFont(f);
        cs->Add(txt, 0, wxALL | wxALIGN_CENTER, 5);

        cs->Add(new wxStaticText(card, wxID_ANY, "Carpeta: " + it.carpeta),
               0, wxALL | wxALIGN_CENTER, 2);

        // Botón AGREGAR
        wxButton* btn = new wxButton(card, wxID_ANY, "AGREGAR");
        wxString data = it.nombre + "|" + "assets/" + it.carpeta + "/" + it.obj +
                       "|" + "assets/" + it.carpeta + "/" + it.tex;
        btn->SetClientObject(new wxStringClientData(data));
        btn->Bind(wxEVT_BUTTON, &DialogoSeleccionModelos::OnBotonUsar, this);

        cs->Add(btn, 0, wxALL | wxALIGN_CENTER, 10);
        card->SetSizer(cs);
        m_gridSizer->Add(card, 1, wxEXPAND);
    }

    // Actualizar contador
    m_contadorModelos->SetLabel(
        wxString::Format("Mostrando %d de %d modelos",
                        modelosMostrados,
                        (int)m_todosLosModelos.size())
    );

    // Refrescar el layout
    m_gridSizer->Layout();
    m_scrollWindow->FitInside();
    m_scrollWindow->Refresh();
}

void DialogoSeleccionModelos::OnBotonUsar(wxCommandEvent& event) {
    wxButton* btn = (wxButton*)event.GetEventObject();
    wxStringClientData* data = (wxStringClientData*)btn->GetClientObject();
    wxString raw = data->GetData();

    wxStringTokenizer tokenizer(raw, "|");
    m_nombreSel = tokenizer.GetNextToken();
    m_rutaObj = tokenizer.GetNextToken();
    m_rutaTex = tokenizer.GetNextToken();

    EndModal(wxID_OK);
}


// ==================================================================
// 3. IMPLEMENTACIÓN DEL FRAME (INTERFAZ)
// ==================================================================

SISTEMA_DIVINCRIFrame::SISTEMA_DIVINCRIFrame(wxFrame *frame, const wxString& title)
    : wxFrame(frame, wxID_ANY, title), m_glCanvas(nullptr), m_timerCoordenadas(this)
{
    Bind(wxEVT_TIMER, &SISTEMA_DIVINCRIFrame::OnTimerCoordenadas, this, m_timerCoordenadas.GetId());
    Maximize(true);

    mainSizer = new wxBoxSizer(wxVERTICAL);
    BuildHeader();

    contentPanel = new wxPanel(this, wxID_ANY);
    wxBoxSizer* contentSizer = new wxBoxSizer(wxVERTICAL);
    contentPanel->SetSizer(contentSizer);

    mainSizer->Add(contentPanel, 1, wxEXPAND);
    SetSizer(mainSizer);

    currentView = nullptr;
    ShowMainPanel();
}

SISTEMA_DIVINCRIFrame::~SISTEMA_DIVINCRIFrame() {}

void SISTEMA_DIVINCRIFrame::BuildHeader() {
    headerPanel = new wxPanel(this, wxID_ANY);
    headerPanel->SetBackgroundColour(*wxWHITE);

    wxBoxSizer* headerSizer = new wxBoxSizer(wxHORIZONTAL);

    wxBoxSizer* titleSizer = new wxBoxSizer(wxVERTICAL);
    wxStaticText* t1 = new wxStaticText(headerPanel, wxID_ANY, "DIVINCRI 3D");
    wxFont font = t1->GetFont();
    font.SetWeight(wxFONTWEIGHT_BOLD);
    font.SetPointSize(14);
    t1->SetFont(font);
    t1->SetForegroundColour(wxColour(0, 100, 0));
    titleSizer->Add(t1, 0, wxALIGN_LEFT);

    wxStaticText* t2 = new wxStaticText(headerPanel, wxID_ANY, L"Editor de escenas forenses · Prototipo académico");
    wxFont fontSub = t2->GetFont();
    fontSub.SetPointSize(10);
    t2->SetFont(fontSub);
    titleSizer->Add(t2, 0, wxALIGN_LEFT | wxTOP, 5);

    headerSizer->Add(titleSizer, 1, wxALIGN_CENTER_VERTICAL | wxLEFT, 15);

    headerPanel->SetSizer(headerSizer);
    mainSizer->Add(headerPanel, 0, wxEXPAND);

    wxStaticLine* line = new wxStaticLine(this);
    line->SetBackgroundColour(wxColour(0, 100, 0));
    mainSizer->Add(line, 0, wxEXPAND);
}

void SISTEMA_DIVINCRIFrame::ShowMainPanel() {
    m_timerCoordenadas.Stop();
    m_glCanvas = nullptr;
    if (currentView) {
        contentPanel->GetSizer()->Detach(currentView);
        currentView->Hide();
        currentView->Destroy();
    }
    currentView = CreateMainDashboard(contentPanel);
    contentPanel->GetSizer()->Add(currentView, 1, wxEXPAND);
    contentPanel->Layout();
}

void SISTEMA_DIVINCRIFrame::ShowSelectionPanel() {
    m_timerCoordenadas.Stop();
    m_glCanvas = nullptr;
    if (currentView) {
        contentPanel->GetSizer()->Detach(currentView);
        currentView->Hide();
        currentView->Destroy();
    }
    currentView = CreateSelectionScreen(contentPanel);
    contentPanel->GetSizer()->Add(currentView, 1, wxEXPAND);
    contentPanel->Layout();
}

void SISTEMA_DIVINCRIFrame::ShowEditor3D(const wxString& sceneName, const wxString& objPath, const wxString& texPath) {
    m_timerCoordenadas.Stop();
    m_glCanvas = nullptr;
    if (currentView) {
        contentPanel->GetSizer()->Detach(currentView);
        currentView->Hide();
        currentView->Destroy();
    }
    currentView = CreateEditor3D(contentPanel, sceneName, objPath, texPath);
    contentPanel->GetSizer()->Add(currentView, 1, wxEXPAND);
    contentPanel->Layout();
}

wxPanel* SISTEMA_DIVINCRIFrame::CreateMainDashboard(wxWindow* parent) {
    wxPanel* p = new wxPanel(parent, wxID_ANY);
    wxBoxSizer* sizer = new wxBoxSizer(wxHORIZONTAL);

    auto* intro = new wxBoxSizer(wxVERTICAL);
    auto* title = new wxStaticText(p, wxID_ANY, L"Recreación de escenas en 3D");
    wxFont heading = title->GetFont();
    heading.MakeBold().SetPointSize(20);
    title->SetFont(heading);
    intro->Add(title, 0, wxBOTTOM, 12);
    intro->Add(new wxStaticText(p, wxID_ANY,
               "Elige una escena, coloca objetos y exporta una captura del visor."),
               0, wxBOTTOM, 18);
    auto* newScene = new wxButton(p, wxID_ANY, "Nueva escena", wxDefaultPosition, wxSize(180, 45));
    newScene->Bind(wxEVT_BUTTON, &SISTEMA_DIVINCRIFrame::OnNewSceneClick, this);
    intro->Add(newScene);
    sizer->Add(intro, 1, wxALL, 35);
    p->SetSizer(sizer);
    return p;
}

wxPanel* SISTEMA_DIVINCRIFrame::CreateSelectionScreen(wxWindow* parent) {
    wxPanel* p = new wxPanel(parent);
    wxBoxSizer* sizer = new wxBoxSizer(wxHORIZONTAL);

    // SIDEBAR
    wxBoxSizer* sidebar = new wxBoxSizer(wxVERTICAL);
    wxButton* btnBack = new wxButton(p, wxID_ANY, "Volver al Panel",wxDefaultPosition, wxSize(200, 50));
    // 1. Color de FONDO (Ejemplo: Un rojo oscuro o azul)
    // Puedes usar constantes como *wxRED, *wxBLUE o definir tu propio RGB
    btnBack->SetBackgroundColour(wxColour(0, 168, 104));

    // 2. Color del TEXTO
    btnBack->SetForegroundColour(*wxWHITE);
    wxFont f1 = btnBack->GetFont(); f1.SetWeight(wxFONTWEIGHT_BOLD); btnBack->SetFont(f1);

    btnBack->Bind(wxEVT_BUTTON, &SISTEMA_DIVINCRIFrame::OnReturnToDashboard, this);
    sidebar->Add(btnBack, 0, wxALL, 10);
    sizer->Add(sidebar, 0, wxEXPAND);
    sizer->Add(new wxStaticLine(p, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLI_VERTICAL), 0, wxEXPAND | wxALL, 10);

    // MAIN AREA
    wxBoxSizer* mainArea = new wxBoxSizer(wxVERTICAL);
    wxStaticText* title = new wxStaticText(p, wxID_ANY, L"Selección de Escena");
    wxFont f = title->GetFont(); f.SetPointSize(14); f.SetWeight(wxFONTWEIGHT_BOLD);
    title->SetFont(f);
    mainArea->Add(title, 0, wxALL, 10);

    wxGridSizer* grid = new wxGridSizer(1, 3, 20, 20);
    wxString names[] = { "Casa Promedio", "Oficina Promedio", "Calle" };

    // Bucle de Tarjetas
    for(int i=0; i<3; i++) {
        wxPanel* card = new wxPanel(p, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_RAISED);
        wxBoxSizer* cs = new wxBoxSizer(wxVERTICAL);

        wxPanel* imgContainer = new wxPanel(card, wxID_ANY, wxDefaultPosition, wxSize(-1, 150), wxBORDER_SIMPLE);
        wxBoxSizer* imgSizer = new wxBoxSizer(wxVERTICAL);
        imgContainer->SetBackgroundColour(*wxWHITE);

        // LOGICA DE IMAGENES (CASA Y OFICINA)
        bool imagenCargada = false;
        wxString archivoImagen = "";

        if (i == 0) archivoImagen = "assets/multimedia/pre-casa.png";    // Casa
        if (i == 1) archivoImagen = "assets/multimedia/pre-oficina.png"; // Oficina
        if (i == 2) archivoImagen = "assets/multimedia/pre-calle.png";

        if (!archivoImagen.IsEmpty()) {
            wxImage preImg;
            if (preImg.LoadFile(archivoImagen, wxBITMAP_TYPE_PNG)) {
                const int width = 290;
                preImg.Rescale(width, preImg.GetHeight() * width / preImg.GetWidth(),
                               wxIMAGE_QUALITY_HIGH);

                wxBitmap bmp(preImg);
                wxStaticBitmap* sb = new wxStaticBitmap(imgContainer, wxID_ANY, bmp);
                imgSizer->AddStretchSpacer();
                imgSizer->Add(sb, 0, wxALIGN_CENTER);
                imgSizer->AddStretchSpacer();
                imagenCargada = true;
            }
        }

        if (!imagenCargada) {
            imgSizer->Add(new wxStaticText(imgContainer, wxID_ANY, names[i]), 0, wxALIGN_CENTER | wxTOP, 60);
            imgSizer->Add(new wxStaticText(imgContainer, wxID_ANY, "(Sin vista previa)"), 0, wxALIGN_CENTER);
        }

        imgContainer->SetSizer(imgSizer);
        cs->Add(imgContainer, 1, wxEXPAND | wxALL, 5);

        cs->Add(new wxStaticText(card, wxID_ANY, names[i]), 0, wxALIGN_CENTER | wxBOTTOM, 5);

        wxBoxSizer* btns = new wxBoxSizer(wxHORIZONTAL);
        wxButton* btnUsar = new wxButton(card, wxID_ANY, "Abrir escena");

        // --- MODIFICADO: GUARDAMOS QUÉ ESCENA ES DENTRO DEL BOTÓN ---
        // Usamos ClientData para pasar el nombre de la escena al evento
        btnUsar->SetClientObject(new wxStringClientData(names[i]));
        btnUsar->Bind(wxEVT_BUTTON, &SISTEMA_DIVINCRIFrame::OnSelectScene, this);

        btns->Add(btnUsar, 1, wxRIGHT, 5);
        cs->Add(btns, 0, wxEXPAND | wxALL, 5);

        card->SetSizer(cs);
        grid->Add(card, 1, wxEXPAND);
    }

    mainArea->Add(grid, 0, wxEXPAND | wxALL, 20);
    sizer->Add(mainArea, 1, wxEXPAND);
    p->SetSizer(sizer);
    return p;
}

wxPanel* SISTEMA_DIVINCRIFrame::CreateEditor3D(wxWindow* parent, const wxString& sceneName, const wxString& objPath, const wxString& texPath) {
    wxPanel* p = new wxPanel(parent);
    wxBoxSizer* sizer = new wxBoxSizer(wxHORIZONTAL);

    // SIDEBAR IZQUIERDA
    wxBoxSizer* tools = new wxBoxSizer(wxVERTICAL);

    wxBoxSizer* top = new wxBoxSizer(wxHORIZONTAL);
    // PRIMERO el botón SALIR (rojo)
    wxButton* exit = new wxButton(p, wxID_ANY, "Salir", wxDefaultPosition, wxSize(110,-1));
    exit->SetBackgroundColour(wxColour(255, 100, 100)); // Rojo
    exit->SetForegroundColour(*wxWHITE);
    wxFont exitFont = exit->GetFont();
    exitFont.SetWeight(wxFONTWEIGHT_BOLD);
    exit->SetFont(exitFont);
    exit->Bind(wxEVT_BUTTON, &SISTEMA_DIVINCRIFrame::OnReturnToDashboard, this);
    top->Add(exit, 0, wxRIGHT, 5);

    tools->Add(top, 0, wxALL, 5);

    wxStaticBoxSizer* objBox = new wxStaticBoxSizer(wxVERTICAL, p, "Objetos en Escena");
    wxButton* btnAdd = new wxButton(p, wxID_ANY, L"Añadir objeto");
    btnAdd->Bind(wxEVT_BUTTON, &SISTEMA_DIVINCRIFrame::OnBtnAddObject, this);
    objBox->Add(btnAdd, 0, wxEXPAND | wxBOTTOM, 5);

    m_checkListObjetos = new wxCheckListBox(p, wxID_ANY, wxDefaultPosition, wxSize(200, 150));
    m_checkListObjetos->Bind(wxEVT_CHECKLISTBOX, &SISTEMA_DIVINCRIFrame::OnCheckListToggled, this);
    m_checkListObjetos->Bind(wxEVT_LISTBOX, &SISTEMA_DIVINCRIFrame::OnListSelected, this);
    objBox->Add(m_checkListObjetos, 1, wxEXPAND);

    wxButton* btnDel = new wxButton(p, wxID_ANY, "Eliminar objeto");
    btnDel->Bind(wxEVT_BUTTON, &SISTEMA_DIVINCRIFrame::OnBtnDeleteObject, this);
    objBox->Add(btnDel, 0, wxEXPAND|wxTOP, 5);
    tools->Add(objBox, 0, wxEXPAND|wxALL, 5);

    // INSTRUCCIONES - MÁS GRANDE VERTICALMENTE
    wxStaticBoxSizer* trBox = new wxStaticBoxSizer(wxVERTICAL, p, "Instrucciones");
    trBox->GetStaticBox()->SetMinSize(wxSize(-1, 140)); // Altura mínima aumentada

    m_lblInstrucciones = new wxStaticText(p, wxID_ANY,
        L"CONTROLES CÁMARA:\n"
        L"• WASD: Movimiento\n"
        L"• Mouse: Rotar vista\n"
        L"• Click: Activar cámara\n"
        L"• ESC: Liberar mouse\n\n"
        L"TRANSFORMACIÓN OBJETOS:\n"
        L"• O/I: Subir/Bajar\n"
        L"• L/K: Izquierda/Derecha\n"
        L"• M/N: Adelante/Atrás\n"
        L"• U/Y: Rotar\n"
        L"• J/H: Escalar\n"
        L"• ESPACIO: Terminar edición");

    m_lblInstrucciones->Wrap(200);
    trBox->Add(m_lblInstrucciones, 0, wxALL, 8);
    tools->Add(trBox, 0, wxEXPAND|wxALL, 5);

    // COORDENADAS DE CÁMARA EN TIEMPO REAL
    wxStaticBoxSizer* coordBox = new wxStaticBoxSizer(wxVERTICAL, p, L"Coordenadas de Cámara");
    m_lblCoordenadasCamara = new wxStaticText(p, wxID_ANY, "X: 0.0  Y: 0.0  Z: 0.0");
    wxFont coordFont = m_lblCoordenadasCamara->GetFont();
    coordFont.SetWeight(wxFONTWEIGHT_BOLD);
    m_lblCoordenadasCamara->SetFont(coordFont);
    coordBox->Add(m_lblCoordenadasCamara, 0, wxALL, 5);
    tools->Add(coordBox, 0, wxEXPAND|wxALL, 5);

    // SABANA DE INDICIOS
    wxStaticBoxSizer* sabanasBox = new wxStaticBoxSizer(wxVERTICAL, p, L"Sábana de indicios");

    wxBoxSizer* sabanasButtons = new wxBoxSizer(wxVERTICAL);
    wxButton* btnCargarSabana = new wxButton(p, wxID_ANY, "Cargar imagen");
    wxButton* btnVerSabana = new wxButton(p, wxID_ANY, "Ver imagen");

    btnCargarSabana->Bind(wxEVT_BUTTON, &SISTEMA_DIVINCRIFrame::OnCargarSabanaIndicios, this);
    btnVerSabana->Bind(wxEVT_BUTTON, &SISTEMA_DIVINCRIFrame::OnVerSabanaIndicios, this);

    sabanasButtons->Add(btnCargarSabana, 0, wxEXPAND|wxBOTTOM, 5);
    sabanasButtons->Add(btnVerSabana, 0, wxEXPAND);
    sabanasBox->Add(sabanasButtons, 0, wxEXPAND|wxALL, 5);
    tools->Add(sabanasBox, 0, wxEXPAND|wxALL, 5);

    // EXPORTAR IMAGEN
    wxStaticBoxSizer* exportBox = new wxStaticBoxSizer(wxVERTICAL, p, "Exportar Imagen");
    wxButton* btnExportar = new wxButton(p, wxID_ANY, "Exportar");
    btnExportar->Bind(wxEVT_BUTTON, &SISTEMA_DIVINCRIFrame::OnExportarImagen, this);
    exportBox->Add(btnExportar, 0, wxEXPAND|wxALL, 5);
    tools->Add(exportBox, 0, wxEXPAND|wxALL, 5);

    sizer->Add(tools, 0, wxEXPAND|wxALL, 5);

    // AREA CENTRAL 3D
    wxBoxSizer* center = new wxBoxSizer(wxVERTICAL);
    wxStaticText* br = new wxStaticText(p, wxID_ANY, "Editor 3D >> " + sceneName);
    wxFont f = br->GetFont(); f.SetWeight(wxFONTWEIGHT_BOLD); br->SetFont(f);
    center->Add(br, 0, wxALL, 5);

    m_glCanvas = new MyGLCanvas(p, wxSize(800, 600), objPath, texPath);
    center->Add(m_glCanvas, 1, wxEXPAND|wxALL, 2);

    // BARRA INFERIOR CON COORDENADAS ACTUALIZADAS EN TIEMPO REAL
    wxPanel* bottomBar = new wxPanel(p, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_SIMPLE);
    wxBoxSizer* bbs = new wxBoxSizer(wxHORIZONTAL);

    // Botones de sabana en la barra inferior también
    wxButton* btnSabanaBar = new wxButton(bottomBar, wxID_ANY, L"Sábana de indicios");
    btnSabanaBar->Bind(wxEVT_BUTTON, &SISTEMA_DIVINCRIFrame::OnVerSabanaIndicios, this);
    bbs->Add(btnSabanaBar, 0, wxALIGN_CENTER_VERTICAL|wxLEFT, 10);

    bbs->AddStretchSpacer();

    // Etiqueta para coordenadas en tiempo real
    m_lblCoordenadasBarra = new wxStaticText(bottomBar, wxID_ANY, L"Coordenadas de Cámara: X: 0.0  Y: 0.0  Z: 0.0");
    bbs->Add(m_lblCoordenadasBarra, 0, wxALIGN_CENTER_VERTICAL|wxALL, 10);

    bottomBar->SetSizer(bbs);
    center->Add(bottomBar, 0, wxEXPAND | wxALL, 2);

    sizer->Add(center, 1, wxEXPAND);
    p->SetSizer(sizer);

    // Iniciar timer para actualizar coordenadas en tiempo real
    m_timerCoordenadas.Start(100);

    // Inicializar la ruta de sabana de indicios
    m_sabanaIndiciosPath = "";

    return p;
}


void SISTEMA_DIVINCRIFrame::OnNewSceneClick(wxCommandEvent& event) { ShowSelectionPanel(); }
void SISTEMA_DIVINCRIFrame::OnReturnToDashboard(wxCommandEvent& event) { ShowMainPanel(); }

void SISTEMA_DIVINCRIFrame::OnSelectScene(wxCommandEvent& event) {
    wxButton* b = (wxButton*)event.GetEventObject();
    wxStringClientData* d = (wxStringClientData*)b->GetClientObject();
    wxString id = d->GetData();

    if(id == "Casa Promedio") ShowEditor3D("Casa Promedio", "assets/casa.obj", "assets/text-wall.jpg");
    else if(id == "Oficina Promedio") ShowEditor3D("Oficina Promedio", "assets/oficina.obj", "assets/text-oficina.jpg");
    else ShowEditor3D("Calle", "assets/calle.obj", "assets/text-calle.jpg");
}

void SISTEMA_DIVINCRIFrame::OnBtnAddObject(wxCommandEvent& event) {
    DialogoSeleccionModelos dlg(this);
    if (dlg.ShowModal() == wxID_OK) {
        wxString nombre = dlg.GetNombreSeleccionado();
        wxString objPath = dlg.GetRutaObj();
        wxString texPath = dlg.GetRutaTex();

        if (!m_glCanvas || !m_glCanvas->AgregarObjetoExtra(nombre, objPath, texPath)) {
            wxMessageBox("No se pudo cargar el modelo seleccionado.", "Modelo 3D", wxOK | wxICON_WARNING, this);
            return;
        }

        int idx = m_checkListObjetos->Append(nombre);
        m_checkListObjetos->Check(idx, true);
        m_checkListObjetos->SetSelection(idx);

        if(m_glCanvas) {
            m_lblInstrucciones->SetLabel("EDITANDO OBJETO:\n[O/I] Arriba/Abajo\n[K/L] Izq/Der\n[U/Y] Rotar\n[J/H] Escalar\n[M/N] Fondo/Frente\n[ESPACIO] Terminar Edicion");
        }
    }
}

void SISTEMA_DIVINCRIFrame::OnCheckListToggled(wxCommandEvent& event) {
    int idx = event.GetInt();
    if(m_glCanvas) m_glCanvas->SetVisibilidad(idx, m_checkListObjetos->IsChecked(idx));
}

void SISTEMA_DIVINCRIFrame::OnListSelected(wxCommandEvent& event) {
    int idx = event.GetSelection();
    if(m_glCanvas) {
        m_glCanvas->SeleccionarObjeto(idx);
        m_lblInstrucciones->SetLabel("EDITANDO OBJETO:\n[O/I] Arriba/Abajo\n[K/L] Izq/Der\n[U/Y] Rotar\n[J/H] Escalar\n[M/N] Fondo/Frente\n[ESPACIO] Terminar Edicion");
    }
}

void SISTEMA_DIVINCRIFrame::OnBtnDeleteObject(wxCommandEvent& event) {
    int idx = m_checkListObjetos->GetSelection();
    if (idx != wxNOT_FOUND && m_glCanvas) {
        m_glCanvas->BorrarObjeto(idx);
        m_checkListObjetos->Delete(idx);
        m_lblInstrucciones->SetLabel(L"Objeto eliminado. Modo\nCámara Libre (WASD).");
    }
}

// ==================================================================
// NUEVOS MÉTODOS PARA SISTEMA_DIVINCRIFrame
// ==================================================================

void SISTEMA_DIVINCRIFrame::OnTimerCoordenadas(wxTimerEvent& event) {
    if (m_glCanvas) {
        // Obtener coordenadas actuales de la cámara desde MyGLCanvas
        wxString coordenadas = m_glCanvas->GetCoordenadasCamara();
        if (m_lblCoordenadasCamara)
            m_lblCoordenadasCamara->SetLabel(coordenadas);
        if (m_lblCoordenadasBarra)
            m_lblCoordenadasBarra->SetLabel(L"Coordenadas de Cámara: " + coordenadas);
    }
}

void SISTEMA_DIVINCRIFrame::OnCargarSabanaIndicios(wxCommandEvent& event) {
    wxFileDialog dialog(this, "Cargar imagen", "", "",
                       "Archivos de imagen (*.png;*.jpg;*.jpeg)|*.png;*.jpg;*.jpeg",
                       wxFD_OPEN | wxFD_FILE_MUST_EXIST);

    if (dialog.ShowModal() == wxID_OK) {
        wxImage image;
        if (!image.LoadFile(dialog.GetPath())) return;
        m_sabanaIndiciosPath = dialog.GetPath();
        wxMessageBox("Sabana de indicios cargada exitosamente", L"Éxito", wxOK | wxICON_INFORMATION);
    }
}

void SISTEMA_DIVINCRIFrame::OnVerSabanaIndicios(wxCommandEvent& event) {
    if (m_sabanaIndiciosPath.empty()) {
        wxMessageBox("Primero debe cargar una sabana de indicios", "Advertencia", wxOK | wxICON_WARNING);
        return;
    }

    // Crear ventana para mostrar la imagen
    wxDialog* dlg = new wxDialog(this, wxID_ANY, L"Sábana de indicios",
                                wxDefaultPosition, wxSize(800, 600));
    wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);

    wxStaticBitmap* bitmap = new wxStaticBitmap(dlg, wxID_ANY,
                                               wxBitmap(m_sabanaIndiciosPath, wxBITMAP_TYPE_ANY),
                                               wxDefaultPosition, wxSize(580, 450));
    sizer->Add(bitmap, 1, wxEXPAND | wxALL, 10);

    wxButton* btnCerrar = new wxButton(dlg, wxID_OK, "Cerrar");
    sizer->Add(btnCerrar, 0, wxALIGN_CENTER | wxBOTTOM, 10);

    dlg->SetSizer(sizer);
    dlg->ShowModal();
    dlg->Destroy();
}

void SISTEMA_DIVINCRIFrame::OnExportarImagen(wxCommandEvent& event) {
    if (m_glCanvas) {
        wxFileDialog dialog(this, "Exportar Imagen", "", "captura_escena.png",
                           "Archivos PNG (*.png)|*.png", wxFD_SAVE | wxFD_OVERWRITE_PROMPT);

        if (dialog.ShowModal() == wxID_OK) {
            wxString filePath = dialog.GetPath();

            // Asegurarse de que la extensión sea .png
            if (!filePath.Lower().EndsWith(".png")) {
                filePath += ".png";
            }

            // Mostrar un mensaje de espera
            wxBusyCursor busyCursor;

            if (m_glCanvas->ExportarCaptura(filePath)) {
                wxMessageBox("Imagen exportada exitosamente a:\n" + filePath,
                           L"Éxito", wxOK | wxICON_INFORMATION);
            } else {
                wxMessageBox(L"Error al exportar la imagen. Verifique que:\n"
                           L"1. El directorio de destino exista\n"
                           L"2. Tenga permisos de escritura\n"
                           L"3. El viewport 3D esté visible",
                           "Error", wxOK | wxICON_ERROR);
            }
        }
    } else {
        wxMessageBox("No hay canvas 3D activo", "Error", wxOK | wxICON_ERROR);
    }
}
