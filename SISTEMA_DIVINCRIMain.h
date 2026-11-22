#ifndef SISTEMA_DIVINCRIMAIN_H
#define SISTEMA_DIVINCRIMAIN_H

#include <wx/wx.h>
#include <wx/glcanvas.h>
#include <vector>
#include <string>

// Estructuras básicas
struct Vertex { float x, y, z; };
struct TexCoord { float u, v; };
struct Normal { float nx, ny, nz; };
struct Face { int vIndex[3], tIndex[3], nIndex[3]; };

// Estructura para un objeto EXTRA (Cuerpo, Marker, Mueble)
struct ObjetoExtra {
    std::string nombre;
    bool visible;

    // Transformación
    float posX, posY, posZ;
    float rotX, rotY, rotZ;
    float scaleX, scaleY, scaleZ;

    // Datos del modelo
    std::vector<Vertex> vertices;
    std::vector<TexCoord> texCoords;
    std::vector<Normal> normals;
    std::vector<Face> faces;
    GLuint textureID;
};

// CLASE DEL CANVAS 3D
class MyGLCanvas : public wxGLCanvas {
public:
    // Constructor original restaurado + rutas
    MyGLCanvas(wxWindow* parent, const wxSize& size, wxString pathOBJ, wxString pathTex);
    virtual ~MyGLCanvas();

    // --- NUEVAS FUNCIONES PARA OBJETOS EXTRA ---
    bool AgregarObjetoExtra(wxString nombre, wxString pathObj, wxString pathTex);
    void SeleccionarObjeto(int index); // -1 para ninguno
    void BorrarObjeto(int index);
    void SetVisibilidad(int index, bool visible);

    // 0: Ninguno (Camara), 1: Mover, 2: Rotar, 3: Escalar
    void SetModoTransformacion(int modo);

    // Agregar estos métodos después de los existentes
    wxString GetCoordenadasCamara() const;
    bool ExportarCaptura(const wxString& filePath);

    // Métodos para acceder a las coordenadas de cámara
    float GetCamX() const { return m_camX; }
    float GetCamY() const { return m_camY; }
    float GetCamZ() const { return m_camZ; }
    float GetCamYaw() const { return m_camYaw; }
    float GetCamPitch() const { return m_camPitch; }

protected:
    void OnPaint(wxPaintEvent& event);
    void OnResize(wxSizeEvent& event);
    void OnMouseLeftClick(wxMouseEvent& event);
    void OnMouseMove(wxMouseEvent& event);
    void OnKeyDown(wxKeyEvent& event); // Aquí manejaremos WASD y Flechas

private:
    void Render(bool present = true);
    // Función auxiliar para cargar cualquier OBJ
    void CargarOBJGenerico(const std::string& path, std::vector<Vertex>& v, std::vector<TexCoord>& vt, std::vector<Normal>& vn, std::vector<Face>& f);
    void CargarTexturaGenerica(const wxString& path, GLuint& texID);

    wxGLContext* m_context;
    bool m_initGL;

    // --- DATOS DE LA ESCENA BASE (CASA/OFICINA) ---
    wxString m_pathBaseTex;
    bool m_baseLoaded;
    GLuint m_baseTextureID;
    std::vector<Vertex> m_baseVertices;
    std::vector<TexCoord> m_baseTexCoords;
    std::vector<Normal> m_baseNormals;
    std::vector<Face> m_baseFaces;

    // --- DATOS DE OBJETOS EXTRA ---
    std::vector<ObjetoExtra> m_objetosExtra;
    int m_indiceSeleccionado; // Cual objeto estamos editando
    int m_modoTransformacion; // 1=Mover, 2=Rotar, 3=Escalar

    // --- CÁMARA (WASD Original) ---
    float m_camX, m_camY, m_camZ;
    float m_camYaw, m_camPitch;
    bool m_isCameraActive;
    wxPoint m_lastMousePos;

    DECLARE_EVENT_TABLE()
};

// CLASE DEL FRAME PRINCIPAL (INTERFAZ)
class SISTEMA_DIVINCRIFrame : public wxFrame {
public:
    SISTEMA_DIVINCRIFrame(wxFrame *frame, const wxString& title);
    ~SISTEMA_DIVINCRIFrame();

private:
    void BuildHeader();

    // Navegación
    void ShowMainPanel();
    void ShowSelectionPanel();
    void ShowEditor3D(const wxString& sceneName, const wxString& objPath, const wxString& texPath);

    // Creadores de Paneles
    wxPanel* CreateMainDashboard(wxWindow* parent);
    wxPanel* CreateSelectionScreen(wxWindow* parent);
    wxPanel* CreateEditor3D(wxWindow* parent, const wxString& sceneName, const wxString& objPath, const wxString& texPath);

    // Eventos Originales
    void OnNewSceneClick(wxCommandEvent& event);
    void OnReturnToDashboard(wxCommandEvent& event);
    void OnSelectScene(wxCommandEvent& event); // Botón "Usar" en selección de escena
    void OnSelectTemplate(wxCommandEvent& event);

    // --- NUEVOS EVENTOS PARA OBJETOS ---
    void OnBtnAddObject(wxCommandEvent& event);      // Botón "Añadir nuevo objeto"
    void OnCheckListToggled(wxCommandEvent& event);  // Check en la lista
    void OnListSelected(wxCommandEvent& event);      // Selección en la lista
    void OnBtnTransform(wxCommandEvent& event);      // Botones Mover/Rotar...
    void OnBtnDeleteObject(wxCommandEvent& event);   // Botón Eliminar



    // Elementos UI
    wxBoxSizer* mainSizer;
    wxPanel* headerPanel;
    wxPanel* contentPanel;
    wxPanel* currentView;

    // Punteros para comunicar UI con Canvas
    MyGLCanvas* m_glCanvas;
    wxCheckListBox* m_checkListObjetos;
    wxStaticText* m_lblInstrucciones;


    // Agregar estas variables después de las existentes
    wxTimer m_timerCoordenadas;
    wxStaticText* m_lblCoordenadasCamara;
    wxStaticText* m_lblCoordenadasBarra;
    wxString m_sabanaIndiciosPath; // Para guardar la ruta de la imagen cargada

    // Agregar estas declaraciones de métodos
    void OnTimerCoordenadas(wxTimerEvent& event);
    void OnCargarSabanaIndicios(wxCommandEvent& event);
    void OnVerSabanaIndicios(wxCommandEvent& event);
    void OnExportarImagen(wxCommandEvent& event);
};

// DIÁLOGO PERSONALIZADO PARA SELECCIONAR MODELOS (JIM/ELI/CAZ)
class DialogoSeleccionModelos : public wxDialog {
public:
    DialogoSeleccionModelos(wxWindow* parent);
    wxString GetNombreSeleccionado() const { return m_nombreSel; }
    wxString GetRutaObj() const { return m_rutaObj; }
    wxString GetRutaTex() const { return m_rutaTex; }

private:
    void OnBotonUsar(wxCommandEvent& event);
    void OnBusqueda(wxCommandEvent& event);  // NUEVO: Evento de búsqueda
    void ActualizarVista(const wxString& filtro);  // NUEVO: Actualizar la vista de tarjetas

    // Estructura para almacenar datos de cada modelo
    struct ItemCatalogo {
        wxString nombre;
        wxString carpeta;
        wxString obj;
        wxString tex;
        wxString img;
    };

    wxString m_nombreSel;
    wxString m_rutaObj;
    wxString m_rutaTex;

    // NUEVOS: Elementos de UI
    wxTextCtrl* m_searchBox;
    wxStaticText* m_contadorModelos;
    wxScrolledWindow* m_scrollWindow;
    wxGridSizer* m_gridSizer;

    std::vector<ItemCatalogo> m_todosLosModelos;  // NUEVO: Lista completa de modelos
};

#endif
