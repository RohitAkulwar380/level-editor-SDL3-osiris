// level-editor.cpp : Standalone Level Editor for Isometric RPG
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3_image/SDL_image.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <array>
#include <fstream>
#include <iostream>

using namespace std;

// Constants (same as main game)
const int MAP_ROWS = 25;
const int MAP_COLS = 25;
const int TILE_SIZE = 32;

// Editor-specific enums
enum class EditorMode {
    PAINT, ERASE
};

enum class TileType {
    EMPTY = 0,
    DIRT = 1,
    GRASS = 2,
    DIRT_PILLAR = 5
};

// Simplified structures for editor
struct SDLState {
    SDL_Window* window;
    SDL_Renderer* renderer;
    int sc_width, sc_height, logW, logH;

    SDLState() {
        sc_width = 1280;
        sc_height = 720;
        logW = 640;
        logH = 320;
    }
};

struct EditorState {
    EditorMode currentMode = EditorMode::PAINT;
    TileType selectedTile = TileType::DIRT;
    bool showGrid = true;
    glm::ivec2 hoveredTile{ -1, -1 };
    bool isMouseDown = false;

    // Editor map data
    short levelMap[MAP_ROWS][MAP_COLS];

    EditorState() {
        // Initialize with default tiles (dirt)
        for (int r = 0; r < MAP_ROWS; r++) {
            for (int c = 0; c < MAP_COLS; c++) {
                levelMap[r][c] = static_cast<short>(TileType::DIRT);
            }
        }
    }
};

struct Resources {
    vector<SDL_Texture*> textures;
    SDL_Texture* texDirt;
    SDL_Texture* texGrass;
    SDL_Texture* texDirtPillar;

    SDL_Texture* loadTexture(SDL_Renderer* renderer, const string& filepath) {
        SDL_Texture* tex = IMG_LoadTexture(renderer, filepath.c_str());
        if (tex) {
            SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_NEAREST);
            textures.push_back(tex);
        }
        else {
            cout << "Failed to load texture: " << filepath << endl;
        }
        return tex;
    }

    void load(SDLState& state) {
        texDirt = loadTexture(state.renderer, "assets/map_assets/tile_003.png");
        texGrass = loadTexture(state.renderer, "assets/map_assets/tile_040.png");
        texDirtPillar = loadTexture(state.renderer, "assets/map_assets/tile_059.png");
    }

    void unload() {
        for (SDL_Texture* tex : textures) {
            SDL_DestroyTexture(tex);
        }
        textures.clear();
    }

    SDL_Texture* getTexture(TileType type) {
        switch (type) {
        case TileType::DIRT: return texDirt;
        case TileType::GRASS: return texGrass;
        case TileType::DIRT_PILLAR: return texDirtPillar;
        default: return texDirt;
        }
    }
};

// Function declarations
bool initialize(SDLState& state);
void cleanup(SDLState& state);
glm::vec2 orthoToIso(int col, int row, int tileSize, const SDLState& state);
glm::ivec2 screenToGrid(int mouseX, int mouseY, const SDLState& state);
void handleInput(SDLState& state, EditorState& editor);
void renderTiles(SDLState& state, EditorState& editor, Resources& res);
void renderGrid(SDLState& state, EditorState& editor);
void renderUI(SDLState& state, EditorState& editor);
void renderMouseHighlight(SDLState& state, EditorState& editor);
void saveLevel(const EditorState& editor, const string& filename);
void loadLevel(EditorState& editor, const string& filename);

int main(int argc, char* argv[]) {
    cout << "Level Editor Starting..." << endl;

    SDLState state;

    if (!initialize(state)) {
        return 1;
    }

    // Load resources
    Resources res;
    res.load(state);

    // Editor state
    EditorState editor;

    // Main loop
    bool running = true;
    while (running) {
        SDL_Event event{ 0 };
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
            case SDL_EVENT_QUIT:
                running = false;
                break;

            case SDL_EVENT_WINDOW_RESIZED:
                state.sc_width = event.window.data1;
                state.sc_height = event.window.data2;
                break;

            case SDL_EVENT_KEY_DOWN:
                // Save level
                if (event.key.key == 's' && (event.key.mod & SDL_KMOD_CTRL)) {
                    saveLevel(editor, "editor_level");
                }
                // Load level  
                if (event.key.key == 'l' && (event.key.mod & SDL_KMOD_CTRL)) {
                    loadLevel(editor, "editor_level");
                }
                break;

            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                if (event.button.button == SDL_BUTTON_LEFT) {
                    editor.isMouseDown = true;
                }
                break;

            case SDL_EVENT_MOUSE_BUTTON_UP:
                if (event.button.button == SDL_BUTTON_LEFT) {
                    editor.isMouseDown = false;
                }
                break;
            }
        }

        // Handle continuous input
        handleInput(state, editor);

        // Render
        SDL_SetRenderDrawColor(state.renderer, 64, 64, 64, 255); // Dark gray background
        SDL_RenderClear(state.renderer);

        // Render tiles
        renderTiles(state, editor, res);

        // Render grid
        if (editor.showGrid) {
            renderGrid(state, editor);
        }

        // Render mouse highlight
        renderMouseHighlight(state, editor);

        // Render UI
        renderUI(state, editor);

        SDL_RenderPresent(state.renderer);
    }

    res.unload();
    cleanup(state);
    cout << "Level Editor Shutting Down..." << endl;
    return 0;
}

bool initialize(SDLState& state) {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Error", "SDL3 Initialization Failed.", 0);
        return false;
    }

    state.window = SDL_CreateWindow("Isometric Level Editor", state.sc_width, state.sc_height, SDL_WINDOW_RESIZABLE);
    if (!state.window) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Error", "Window Creation Failed.", 0);
        return false;
    }

    state.renderer = SDL_CreateRenderer(state.window, nullptr);
    if (!state.renderer) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Error", "Renderer Creation Failed.", state.window);
        cleanup(state);
        return false;
    }

    SDL_SetRenderLogicalPresentation(state.renderer, state.logW, state.logH, SDL_LOGICAL_PRESENTATION_LETTERBOX);
    return true;
}

void cleanup(SDLState& state) {
    SDL_DestroyRenderer(state.renderer);
    SDL_DestroyWindow(state.window);
    SDL_Quit();
}

glm::vec2 orthoToIso(int col, int row, int tileSize, const SDLState& state) {
    // Use the exact same conversion as your main game
    int tileWidth = tileSize;
    int tileHeight = tileSize / 2;

    float isoX = (col - row) * (tileWidth / 2.0f);
    float isoY = (col + row) * (tileHeight / 2.0f);

    float offsetX = state.logW / 2;
    float offsetY = 0;

    return glm::vec2(isoX + offsetX, isoY + offsetY);
}

glm::ivec2 screenToGrid(int mouseX, int mouseY, const SDLState& state) {
    // Convert screen coordinates to logical coordinates
    float logicalX = (float)mouseX * state.logW / state.sc_width;
    float logicalY = (float)mouseY * state.logH / state.sc_height;

    // Adjust for isometric offset
    float offsetX = state.logW / 2;
    float offsetY = 0;

    float adjustedX = logicalX - offsetX;
    float adjustedY = logicalY - offsetY;

    // Convert back to grid coordinates (inverse of orthoToIso)
    int tileWidth = TILE_SIZE;
    int tileHeight = TILE_SIZE / 2;

    float tempCol = (adjustedX / (tileWidth / 2.0f) + adjustedY / (tileHeight / 2.0f)) / 2.0f;
    float tempRow = (adjustedY / (tileHeight / 2.0f) - adjustedX / (tileWidth / 2.0f)) / 2.0f;

    int col = static_cast<int>(tempCol + 0.5f); // Round to nearest
    int row = static_cast<int>(tempRow + 0.5f); // Round to nearest

    // Clamp to valid range
    col = max(0, min(MAP_COLS - 1, col));
    row = max(0, min(MAP_ROWS - 1, row));

    return { col, row };
}

void handleInput(SDLState& state, EditorState& editor) {
    // Get mouse position
    float mouseX, mouseY;
    SDL_GetMouseState(&mouseX, &mouseY);

    // Update hovered tile
    editor.hoveredTile = screenToGrid((int)mouseX, (int)mouseY, state);

    // Handle tile painting
    if (editor.isMouseDown) {
        int col = editor.hoveredTile.x;
        int row = editor.hoveredTile.y;

        if (col >= 0 && col < MAP_COLS && row >= 0 && row < MAP_ROWS) {
            if (editor.currentMode == EditorMode::PAINT) {
                editor.levelMap[row][col] = static_cast<short>(editor.selectedTile);
            }
            else if (editor.currentMode == EditorMode::ERASE) {
                editor.levelMap[row][col] = static_cast<short>(TileType::EMPTY);
            }
        }
    }

    // Keyboard shortcuts using SDL3 keyboard state
    const bool* keyboardState = SDL_GetKeyboardState(nullptr);

    if (keyboardState[SDL_SCANCODE_1]) editor.selectedTile = TileType::DIRT;
    if (keyboardState[SDL_SCANCODE_2]) editor.selectedTile = TileType::GRASS;
    if (keyboardState[SDL_SCANCODE_3]) editor.selectedTile = TileType::DIRT_PILLAR;

    if (keyboardState[SDL_SCANCODE_P]) editor.currentMode = EditorMode::PAINT;
    if (keyboardState[SDL_SCANCODE_E]) editor.currentMode = EditorMode::ERASE;

    // Toggle grid
    static bool gKeyPressed = false;
    if (keyboardState[SDL_SCANCODE_G]) {
        if (!gKeyPressed) {
            editor.showGrid = !editor.showGrid;
            gKeyPressed = true;
        }
    }
    else {
        gKeyPressed = false;
    }
}

void renderTiles(SDLState& state, EditorState& editor, Resources& res) {
    for (int r = 0; r < MAP_ROWS; r++) {
        for (int c = 0; c < MAP_COLS; c++) {
            TileType tileType = static_cast<TileType>(editor.levelMap[r][c]);

            if (tileType != TileType::EMPTY) {
                glm::vec2 isoPos = orthoToIso(c, r, TILE_SIZE, state);

                SDL_FRect dst = {
                    .x = isoPos.x,
                    .y = isoPos.y,
                    .w = TILE_SIZE,
                    .h = TILE_SIZE
                };

                SDL_Texture* texture = res.getTexture(tileType);
                if (texture) {
                    SDL_RenderTexture(state.renderer, texture, nullptr, &dst);
                }
            }
        }
    }
}

void renderGrid(SDLState& state, EditorState& editor) {
    SDL_SetRenderDrawColor(state.renderer, 255, 255, 255, 80); // Semi-transparent white

    for (int r = 0; r <= MAP_ROWS; r++) {
        for (int c = 0; c <= MAP_COLS; c++) {
            // Skip if we're outside the actual grid bounds
            if (r >= MAP_ROWS && c >= MAP_COLS) continue;

            glm::vec2 isoPos = orthoToIso(c, r, TILE_SIZE, state);

            // Draw horizontal grid lines (connecting to the right)
            if (c < MAP_COLS) {
                glm::vec2 rightPos = orthoToIso(c + 1, r, TILE_SIZE, state);
                SDL_RenderLine(state.renderer,
                    isoPos.x, isoPos.y + TILE_SIZE / 2,
                    rightPos.x, rightPos.y + TILE_SIZE / 2);
            }

            // Draw vertical grid lines (connecting downward)
            if (r < MAP_ROWS) {
                glm::vec2 downPos = orthoToIso(c, r + 1, TILE_SIZE, state);
                SDL_RenderLine(state.renderer,
                    isoPos.x + TILE_SIZE / 2, isoPos.y,
                    downPos.x + TILE_SIZE / 2, downPos.y);
            }
        }
    }
}

void renderMouseHighlight(SDLState& state, EditorState& editor) {
    if (editor.hoveredTile.x >= 0 && editor.hoveredTile.y >= 0 &&
        editor.hoveredTile.x < MAP_COLS && editor.hoveredTile.y < MAP_ROWS) {

        glm::vec2 isoPos = orthoToIso(editor.hoveredTile.x, editor.hoveredTile.y, TILE_SIZE, state);

        // Yellow highlight with transparency
        SDL_SetRenderDrawColor(state.renderer, 255, 255, 0, 100);
        SDL_FRect highlight = {
            .x = isoPos.x,
            .y = isoPos.y,
            .w = TILE_SIZE,
            .h = TILE_SIZE
        };
        SDL_RenderFillRect(state.renderer, &highlight);

        // Yellow border
        SDL_SetRenderDrawColor(state.renderer, 255, 255, 0, 255);
        SDL_RenderRect(state.renderer, &highlight);
    }
}

void renderUI(SDLState& state, EditorState& editor) {
    // UI Panel background
    SDL_SetRenderDrawColor(state.renderer, 0, 0, 0, 200);
    SDL_FRect panel = { 5, 5, 150, 100 };
    SDL_RenderFillRect(state.renderer, &panel);

    SDL_SetRenderDrawColor(state.renderer, 255, 255, 255, 255);
    SDL_RenderRect(state.renderer, &panel);

    // Tile selection buttons
    int buttonY = 15;
    int buttonSize = 20;
    int buttonSpacing = 25;

    // Dirt button (brown)
    SDL_SetRenderDrawColor(state.renderer,
        editor.selectedTile == TileType::DIRT ? 255 : 139,
        editor.selectedTile == TileType::DIRT ? 255 : 69,
        editor.selectedTile == TileType::DIRT ? 0 : 19, 255);
    SDL_FRect dirtBtn = { 15, buttonY, buttonSize, buttonSize };
    SDL_RenderFillRect(state.renderer, &dirtBtn);

    // Grass button (green)
    SDL_SetRenderDrawColor(state.renderer,
        editor.selectedTile == TileType::GRASS ? 255 : 34,
        editor.selectedTile == TileType::GRASS ? 255 : 139,
        editor.selectedTile == TileType::GRASS ? 0 : 34, 255);
    SDL_FRect grassBtn = { 15 + buttonSpacing, buttonY, buttonSize, buttonSize };
    SDL_RenderFillRect(state.renderer, &grassBtn);

    // Dirt Pillar button (dark brown)
    SDL_SetRenderDrawColor(state.renderer,
        editor.selectedTile == TileType::DIRT_PILLAR ? 255 : 101,
        editor.selectedTile == TileType::DIRT_PILLAR ? 255 : 67,
        editor.selectedTile == TileType::DIRT_PILLAR ? 0 : 33, 255);
    SDL_FRect pillarBtn = { 15 + buttonSpacing * 2, buttonY, buttonSize, buttonSize };
    SDL_RenderFillRect(state.renderer, &pillarBtn);

    // Mode indicator
    buttonY += 30;
    SDL_SetRenderDrawColor(state.renderer,
        editor.currentMode == EditorMode::PAINT ? 0 : 255,
        editor.currentMode == EditorMode::PAINT ? 255 : 0,
        0, 255);
    SDL_FRect modeBtn = { 15, buttonY, 40, 15 };
    SDL_RenderFillRect(state.renderer, &modeBtn);

    // Status text area (placeholder for now)
    SDL_SetRenderDrawColor(state.renderer, 50, 50, 50, 255);
    SDL_FRect statusArea = { 15, buttonY + 20, 120, 20 };
    SDL_RenderFillRect(state.renderer, &statusArea);
}

void saveLevel(const EditorState& editor, const string& filename) {
    ofstream file(filename + ".csv");
    if (!file.is_open()) {
        cout << "Failed to open file for writing: " << filename << ".csv" << endl;
        return;
    }

    for (int r = 0; r < MAP_ROWS; r++) {
        for (int c = 0; c < MAP_COLS; c++) {
            file << editor.levelMap[r][c];
            if (c < MAP_COLS - 1) file << ",";
        }
        file << "\n";
    }

    file.close();
    cout << "Level saved successfully: " << filename << ".csv" << endl;
}

void loadLevel(EditorState& editor, const string& filename) {
    ifstream file(filename + ".csv");
    if (!file.is_open()) {
        cout << "Failed to open file for reading: " << filename << ".csv" << endl;
        return;
    }

    string line;
    int row = 0;

    while (getline(file, line) && row < MAP_ROWS) {
        int col = 0;
        size_t pos = 0;

        while (pos < line.length() && col < MAP_COLS) {
            size_t nextComma = line.find(',', pos);
            if (nextComma == string::npos) nextComma = line.length();

            string valueStr = line.substr(pos, nextComma - pos);
            editor.levelMap[row][col] = stoi(valueStr);

            col++;
            pos = nextComma + 1;
        }
        row++;
    }

    file.close();
    cout << "Level loaded successfully: " << filename << ".csv" << endl;
}