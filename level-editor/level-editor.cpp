#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <SDL3_image/SDL_image.h>
#include <iostream>
#include <vector>
#include <cmath>
#include <algorithm>
#include <string>
#include <fstream>
#include <sstream> 
#include <cstdio> 
#include <stdexcept>

// --- Configuration and Constants ---
const int INITIAL_SCREEN_WIDTH = 1280;
const int INITIAL_SCREEN_HEIGHT = 720;
const int TILE_PANEL_WIDTH = 250;
int gMapSize = 25;  // Match game's MAP_ROWS/MAP_COLS
const float BASE_TILE_WIDTH = 32.0f;  // Match game's TILE_SIZE

const std::string SAVE_PATH = "D:\\codingStudy\\IMED MCA\\Sem - 3\\Project\\level-editor-SDL3-osiris\\assets\\levels\\";
const std::string ASSET_PATH = "D:\\codingStudy\\IMED MCA\\Sem - 3\\Project\\level-editor-SDL3-osiris\\assets\\isometric tileset\\separated images\\";

// --- Data Structures ---
struct TileData {
    int height;
    int tileID;
};

struct TileType {
    std::string name;
    int tileID;
    SDL_Texture* texture;
};

// Define available tile types with their IDs (kept for backwards compatibility)
std::vector<TileType> gTileTypes = {
    {"Grass", 0, nullptr},
    {"Dirt", 1, nullptr},
    {"Stone", 2, nullptr},
    {"Sand", 3, nullptr},
    {"Water", 4, nullptr},
    {"Snow", 5, nullptr},
    {"Lava", 6, nullptr},
    {"Wood", 7, nullptr}
};

int gSelectedTileType = 0;

struct Level {
    std::vector<std::vector<TileData>> map;
    float zoomLevel = 1.0f;
    float mapOffsetX = 0.0f;
    float mapOffsetY = 0.0f;
    float targetOffsetX = 0.0f;
    float targetOffsetY = 0.0f;
};

// --- Global State ---
SDL_Window* gWindow = nullptr;
SDL_Renderer* gRenderer = nullptr;
TTF_Font* gFont = nullptr;
std::vector<Level> gLevels;
size_t gCurrentLevelIndex = 0;
bool gIsDragging = false;
float gLastMouseX = 0.0f;
float gLastMouseY = 0.0f;
float gMouseDownX = 0.0f;
float gMouseDownY = 0.0f;
bool gIsSnapping = false;
const float SNAP_SPEED = 0.1f;
const float DRAG_THRESHOLD = 5.0f; // pixels

bool gIsEditingGridSize = false;
std::string gGridSizeInput = "25";  // Match default map size

// Tile texture cache
std::vector<SDL_Texture*> gTileTextures;

// Tile panel scrolling
float gTilePanelScroll = 0.0f;
bool gIsDraggingScrollbar = false;
float gScrollbarDragOffset = 0.0f;

// --- Utility Functions ---
SDL_Texture* loadTexture(const std::string& path) {
    SDL_Surface* surface = IMG_Load(path.c_str());
    if (!surface) {
        std::cerr << "Failed to load image: " << path << " - IMG Error: " << SDL_GetError() << std::endl;
        return nullptr;
    }
    SDL_Texture* texture = SDL_CreateTextureFromSurface(gRenderer, surface);
    SDL_DestroySurface(surface);
    if (!texture) {
        std::cerr << "Failed to create texture from surface: " << SDL_GetError() << std::endl;
    }
    return texture;
}

void loadTileTextures() {
    // Load textures for tile types
    for (auto& tileType : gTileTypes) {
        std::string path = ASSET_PATH + "tile_" +
            std::string(3 - std::min(3, (int)std::to_string(tileType.tileID).length()), '0') +
            std::to_string(tileType.tileID) + ".png";
        tileType.texture = loadTexture(path);
        if (!tileType.texture) {
            std::cerr << "Warning: Could not load texture for " << tileType.name << std::endl;
        }
    }

    // Preload all tile textures (0-114)
    for (int i = 0; i <= 114; i++) {
        std::string path = ASSET_PATH + "tile_" +
            std::string(3 - std::min(3, (int)std::to_string(i).length()), '0') +
            std::to_string(i) + ".png";
        SDL_Texture* tex = loadTexture(path);
        gTileTextures.push_back(tex);
    }
    std::cout << "[Status] Loaded " << gTileTextures.size() << " tile textures." << std::endl;
}

void freeTileTextures() {
    for (auto& tileType : gTileTypes) {
        if (tileType.texture) {
            SDL_DestroyTexture(tileType.texture);
            tileType.texture = nullptr;
        }
    }
    for (auto* tex : gTileTextures) {
        if (tex) {
            SDL_DestroyTexture(tex);
        }
    }
    gTileTextures.clear();
}

void renderText(const std::string& text, float x, float y, SDL_Color color) {
    if (!gFont || text.empty()) return;
    SDL_Surface* surface = TTF_RenderText_Solid(gFont, text.c_str(), 0, color);
    if (!surface) return;
    SDL_Texture* texture = SDL_CreateTextureFromSurface(gRenderer, surface);
    SDL_FRect destRect = { x, y, (float)surface->w, (float)surface->h };
    SDL_RenderTexture(gRenderer, texture, nullptr, &destRect);
    SDL_DestroyTexture(texture);
    SDL_DestroySurface(surface);
}

std::vector<std::vector<TileData>> generateMap(int mapSize) {
    std::vector<std::vector<TileData>> map;
    map.resize(mapSize, std::vector<TileData>(mapSize));
    for (int x = 0; x < mapSize; ++x) {
        for (int y = 0; y < mapSize; ++y) {
            map[x][y].height = 0;
            map[x][y].tileID = 0;
        }
    }
    return map;
}

// --- Level State Management ---
void addInitialLevels(int count) {
    if (gLevels.empty()) {
        for (int i = 0; i < count; ++i) {
            gLevels.push_back({ generateMap(gMapSize) });
        }
    }
}

void addNewLevel() {
    gLevels.push_back({ generateMap(gMapSize) });
    gCurrentLevelIndex = gLevels.size() - 1;
    std::cout << "[Status] New level created (" << gMapSize << "x" << gMapSize << "). You are now on Level " << gCurrentLevelIndex + 1 << "." << std::endl;
}

void deleteCurrentLevel() {
    if (gLevels.size() > 1) {
        std::cout << "[Status] Level " << gCurrentLevelIndex + 1 << " deleted." << std::endl;
        gLevels.erase(gLevels.begin() + gCurrentLevelIndex);
        if (gCurrentLevelIndex >= gLevels.size()) {
            gCurrentLevelIndex = gLevels.size() - 1;
        }
    }
    else {
        std::cerr << "Cannot delete the last level." << std::endl;
    }
}

// --- Serialization (Multi-File CSV) ---
bool saveLevels() {
    std::cout << "[Action] Saving all levels to separate files..." << std::endl;
    for (size_t i = 0; i < gLevels.size(); ++i) {
        std::string filename = SAVE_PATH + "level_" + std::to_string(i + 1) + ".csv";
        std::ofstream file(filename);
        if (!file.is_open()) {
            std::cerr << "Error: Could not save " << filename << ". Please ensure the path exists." << std::endl;
            continue;
        }
        const auto& level = gLevels[i];
        int mapSize = (int)level.map.size();
        file << level.zoomLevel << "," << level.mapOffsetX << "," << level.mapOffsetY << "," << mapSize << "\n";

        // Save height data
        for (int y = 0; y < mapSize; ++y) {
            for (int x = 0; x < mapSize; ++x) {
                file << level.map[x][y].height << (x < mapSize - 1 ? "," : "");
            }
            file << "\n";
        }

        // Save tile ID data
        for (int y = 0; y < mapSize; ++y) {
            for (int x = 0; x < mapSize; ++x) {
                file << level.map[x][y].tileID << (x < mapSize - 1 ? "," : "");
            }
            file << "\n";
        }

        file.close();
    }
    size_t levelIndexToDelete = gLevels.size() + 1;
    while (true) {
        std::string filename_to_check = SAVE_PATH + "level_" + std::to_string(levelIndexToDelete) + ".csv";
        if (std::ifstream(filename_to_check).good()) {
            if (std::remove(filename_to_check.c_str()) == 0) {
                std::cout << "[Status] Cleaned up orphan file: " << filename_to_check << std::endl;
            }
        }
        else {
            break;
        }
        levelIndexToDelete++;
    }
    std::cout << "[Status] Save successful." << std::endl;
    return true;
}

bool loadLevels() {
    std::cout << "[Action] Searching for level files in " << SAVE_PATH << "..." << std::endl;
    gLevels.clear();
    int levelIndex = 1;
    while (true) {
        std::string filename = SAVE_PATH + "level_" + std::to_string(levelIndex) + ".csv";
        std::ifstream file(filename);
        if (!file.is_open()) {
            break;
        }
        try {
            Level level;
            std::string line, value;
            if (!std::getline(file, line) || line.empty()) throw std::runtime_error("File is empty or header is missing.");
            std::stringstream header_ss(line);
            if (!std::getline(header_ss, value, ',')) throw std::runtime_error("Incomplete header.");
            level.zoomLevel = std::stof(value);
            if (!std::getline(header_ss, value, ',')) throw std::runtime_error("Incomplete header.");
            level.mapOffsetX = std::stof(value);
            if (!std::getline(header_ss, value, ',')) throw std::runtime_error("Incomplete header.");
            level.mapOffsetY = std::stof(value);
            int mapSize = 0;
            if (std::getline(header_ss, value, ',')) {
                mapSize = std::stoi(value);
            }
            else {
                throw std::runtime_error("Map size is missing in header.");
            }
            if (mapSize <= 0) throw std::runtime_error("Invalid map size in file.");
            level.targetOffsetX = level.mapOffsetX;
            level.targetOffsetY = level.mapOffsetY;
            level.map = generateMap(mapSize);

            // Load height data
            for (int y = 0; y < mapSize; ++y) {
                if (!std::getline(file, line)) throw std::runtime_error("Map height data is incomplete.");
                std::stringstream map_row_ss(line);
                for (int x = 0; x < mapSize; ++x) {
                    if (!std::getline(map_row_ss, value, ',')) throw std::runtime_error("Map row is incomplete.");
                    level.map[x][y].height = std::stoi(value);
                }
            }

            // Load tile ID data (if available)
            for (int y = 0; y < mapSize; ++y) {
                if (!std::getline(file, line)) {
                    break;
                }
                std::stringstream map_row_ss(line);
                for (int x = 0; x < mapSize; ++x) {
                    if (!std::getline(map_row_ss, value, ',')) break;
                    level.map[x][y].tileID = std::stoi(value);
                }
            }

            gLevels.push_back(level);
        }
        catch (const std::exception& e) {
            std::cerr << "Error loading " << filename << ": " << e.what() << " Skipping file." << std::endl;
        }
        levelIndex++;
    }
    if (!gLevels.empty()) {
        gCurrentLevelIndex = 0;
        gMapSize = (int)gLevels[0].map.size();
        gGridSizeInput = std::to_string(gMapSize);
        std::cout << "[Status] " << gLevels.size() << " level(s) successfully retrieved." << std::endl;
        return true;
    }
    std::cerr << "Info: No level files found. Starting new session." << std::endl;
    return false;
}

// --- Coordinate Systems and Camera ---
void updateTileSizes(float zoomLevel, float& tileWidth, float& tileHeight) {
    tileWidth = BASE_TILE_WIDTH * zoomLevel;
    tileHeight = tileWidth / 2.0f;
}

SDL_FPoint getMapViewCenter() {
    int windowW, windowH;
    SDL_GetWindowSize(gWindow, &windowW, &windowH);
    return { (float)(windowW - TILE_PANEL_WIDTH) / 2.0f, (float)windowH / 2.0f };
}

SDL_FPoint cartesianToScreen(float x, float y, float tileWidth, float tileHeight, float mapOffsetX, float mapOffsetY) {
    // Match the game's isometric projection
    float px = (x - y) * (tileWidth / 2.0f);
    float py = (x + y) * (tileHeight / 2.0f);
    SDL_FPoint center = getMapViewCenter();
    px += center.x + mapOffsetX;
    py += center.y + mapOffsetY;
    return { px, py };
}

SDL_FPoint screenToCartesian(float px, float py, float tileWidth, float tileHeight, float mapOffsetX, float mapOffsetY) {
    SDL_FPoint center = getMapViewCenter();
    px -= center.x + mapOffsetX;
    py -= center.y + mapOffsetY;
    float x = (px / tileWidth + py / tileHeight);
    float y = (py / tileHeight - px / tileWidth);
    return { x, y };
}

void snapToNearestTile() {
    if (gLevels.empty()) return;
    Level& currentLevel = gLevels[gCurrentLevelIndex];
    int mapSize = (int)currentLevel.map.size();
    float tileWidth, tileHeight;
    updateTileSizes(currentLevel.zoomLevel, tileWidth, tileHeight);
    SDL_FPoint center = getMapViewCenter();
    SDL_FPoint cart = screenToCartesian(center.x, center.y, tileWidth, tileHeight, 0.0f, 0.0f);
    int targetX = (int)std::round(cart.x);
    int targetY = (int)std::round(cart.y);
    targetX = std::max(0, std::min(mapSize - 1, targetX));
    targetY = std::max(0, std::min(mapSize - 1, targetY));
    SDL_FPoint targetScreenPos = cartesianToScreen((float)targetX, (float)targetY, tileWidth, tileHeight, 0.0f, 0.0f);
    currentLevel.targetOffsetX = center.x - targetScreenPos.x;
    currentLevel.targetOffsetY = center.y - targetScreenPos.y;
    gIsSnapping = true;
}

void updateCamera() {
    if (gLevels.empty() || !gIsSnapping) return;
    Level& currentLevel = gLevels[gCurrentLevelIndex];
    currentLevel.mapOffsetX += (currentLevel.targetOffsetX - currentLevel.mapOffsetX) * SNAP_SPEED;
    currentLevel.mapOffsetY += (currentLevel.targetOffsetY - currentLevel.mapOffsetY) * SNAP_SPEED;
    if (std::abs(currentLevel.targetOffsetX - currentLevel.mapOffsetX) < 1.0f && std::abs(currentLevel.targetOffsetY - currentLevel.mapOffsetY) < 1.0f) {
        currentLevel.mapOffsetX = currentLevel.targetOffsetX;
        currentLevel.mapOffsetY = currentLevel.targetOffsetY;
        gIsSnapping = false;
    }
}

// --- Asset Loading and Cleanup ---
bool init() {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::cerr << "SDL_Init Error: " << SDL_GetError() << std::endl;
        return false;
    }

    if (!TTF_Init()) {
        std::cerr << "SDL_ttf could not initialize! TTF_Error: " << SDL_GetError() << std::endl;
        return false;
    }

    gWindow = SDL_CreateWindow("Osiris Level Editor (SDL3)", INITIAL_SCREEN_WIDTH, INITIAL_SCREEN_HEIGHT, SDL_WINDOW_RESIZABLE);
    if (gWindow == nullptr) {
        std::cerr << "Window creation failed: " << SDL_GetError() << std::endl;
        return false;
    }

    gRenderer = SDL_CreateRenderer(gWindow, nullptr);
    if (gRenderer == nullptr) {
        std::cerr << "Renderer creation failed: " << SDL_GetError() << std::endl;
        return false;
    }

    gFont = TTF_OpenFont("font.ttf", 16);
    if (gFont == nullptr) {
        std::cerr << "Failed to load font! TTF_Error: " << SDL_GetError() << std::endl;
    }

    SDL_SetRenderDrawBlendMode(gRenderer, SDL_BLENDMODE_BLEND);

    loadTileTextures();

    return true;
}

void close() {
    saveLevels();
    freeTileTextures();
    if (gFont) {
        TTF_CloseFont(gFont);
    }
    SDL_DestroyRenderer(gRenderer);
    SDL_DestroyWindow(gWindow);
    TTF_Quit();
    SDL_Quit();
}

// --- Rendering Functions ---
void drawTilePanel() {
    int windowW, windowH;
    SDL_GetWindowSize(gWindow, &windowW, &windowH);

    float panelX = (float)(windowW - TILE_PANEL_WIDTH);

    // Draw panel background
    SDL_SetRenderDrawColor(gRenderer, 40, 40, 40, 255);
    SDL_FRect panelBg = { panelX, 0, (float)TILE_PANEL_WIDTH, (float)windowH };
    SDL_RenderFillRect(gRenderer, &panelBg);

    // Draw panel border
    SDL_SetRenderDrawColor(gRenderer, 80, 80, 80, 255);
    SDL_RenderRect(gRenderer, &panelBg);

    // Draw title
    SDL_Color titleColor = { 220, 220, 220, 255 };
    renderText("All Tiles", panelX + 10, 15, titleColor);

    // Calculate content dimensions - show ALL tiles (0-114)
    float tileY = 50;
    float tileSize = 60;
    float spacing = 8;
    int totalTiles = (int)gTileTextures.size();
    float contentHeight = totalTiles * (tileSize + spacing);
    float viewportHeight = windowH - tileY - 10;

    // Calculate max scroll
    float maxScroll = std::max(0.0f, contentHeight - viewportHeight);
    gTilePanelScroll = std::max(0.0f, std::min(maxScroll, gTilePanelScroll));

    // Enable scissor test for clipping
    SDL_Rect clipRect = { (int)panelX, (int)tileY, TILE_PANEL_WIDTH, (int)viewportHeight };
    SDL_SetRenderClipRect(gRenderer, &clipRect);

    // Draw all tile options
    for (int i = 0; i < totalTiles; ++i) {
        float itemX = panelX + 10;
        float itemY = tileY + (i * (tileSize + spacing)) - gTilePanelScroll;

        // Skip if not visible
        if (itemY + tileSize < tileY || itemY > tileY + viewportHeight) continue;

        // Draw selection highlight
        if (i == gSelectedTileType) {
            SDL_SetRenderDrawColor(gRenderer, 100, 150, 255, 255);
            SDL_FRect highlightRect = { itemX - 5, itemY - 3, (float)TILE_PANEL_WIDTH - 20, tileSize + 6 };
            SDL_RenderFillRect(gRenderer, &highlightRect);
        }

        // Draw tile preview using actual texture
        if (i < (int)gTileTextures.size() && gTileTextures[i]) {
            float previewSize = 50;
            float centerX = itemX + 25;
            float centerY = itemY + tileSize / 2;

            SDL_FRect destRect = {
                centerX - previewSize / 2,
                centerY - previewSize / 2,
                previewSize,
                previewSize
            };

            SDL_RenderTexture(gRenderer, gTileTextures[i], nullptr, &destRect);
        }

        // Draw tile ID
        SDL_Color textColor = { 220, 220, 220, 255 };
        std::string tileLabel = "Tile " + std::to_string(i);
        renderText(tileLabel, itemX + 65, itemY + 22, textColor);
    }

    // Disable scissor test
    SDL_SetRenderClipRect(gRenderer, nullptr);

    // Draw scrollbar if needed
    if (contentHeight > viewportHeight) {
        float scrollbarWidth = 8;
        float scrollbarX = panelX + TILE_PANEL_WIDTH - scrollbarWidth - 5;
        float scrollbarY = tileY;
        float scrollbarHeight = viewportHeight;

        // Scrollbar track
        SDL_SetRenderDrawColor(gRenderer, 60, 60, 60, 255);
        SDL_FRect trackRect = { scrollbarX, scrollbarY, scrollbarWidth, scrollbarHeight };
        SDL_RenderFillRect(gRenderer, &trackRect);

        // Scrollbar thumb
        float thumbHeight = (viewportHeight / contentHeight) * scrollbarHeight;
        thumbHeight = std::max(20.0f, thumbHeight);
        float thumbY = scrollbarY + (gTilePanelScroll / maxScroll) * (scrollbarHeight - thumbHeight);

        SDL_SetRenderDrawColor(gRenderer, 120, 120, 120, 255);
        SDL_FRect thumbRect = { scrollbarX, thumbY, scrollbarWidth, thumbHeight };
        SDL_RenderFillRect(gRenderer, &thumbRect);
    }
}

void drawTile(int x, int y, const TileData& data, float tileWidth, float tileHeight, float mapOffsetX, float mapOffsetY) {
    SDL_FPoint screenPos = cartesianToScreen((float)x, (float)y, tileWidth, tileHeight, mapOffsetX, mapOffsetY);
    float px = screenPos.x;
    float py = screenPos.y;
    int h = data.height;

    int windowW, windowH;
    SDL_GetWindowSize(gWindow, &windowW, &windowH);
    if (px + tileWidth < 0 || px - tileWidth > windowW || py + tileHeight < 0 || py - tileHeight - (10 * tileHeight) > windowH) {
        return;
    }

    float height_offset = h * (tileHeight * 0.5f);

    // Get the appropriate texture
    SDL_Texture* tileTexture = nullptr;
    if (data.tileID >= 0 && data.tileID < (int)gTileTextures.size()) {
        tileTexture = gTileTextures[data.tileID];
    }

    if (tileTexture) {
        // Render texture
        SDL_FRect destRect = {
            px - tileWidth / 2.0f,
            py - height_offset - tileHeight / 2.0f,
            tileWidth,
            tileHeight
        };
        SDL_RenderTexture(gRenderer, tileTexture, nullptr, &destRect);
    }

    // Draw height extrusion if height > 0
    if (h > 0) {
        SDL_FPoint basePoint = { px, py + tileHeight };
        SDL_FPoint baseLeft = { px - tileWidth / 2.0f, py + tileHeight / 2.0f };
        SDL_FPoint baseRight = { px + tileWidth / 2.0f, py + tileHeight / 2.0f };
        SDL_FPoint topLeft = { px - tileWidth / 2.0f, py + tileHeight / 2.0f - height_offset };
        SDL_FPoint topRight = { px + tileWidth / 2.0f, py + tileHeight / 2.0f - height_offset };
        SDL_FPoint topPoint = { px, py + tileHeight - height_offset };

        // Left side (darker)
        SDL_SetRenderDrawColor(gRenderer, 60, 60, 60, 200);
        SDL_Vertex leftVertices[4];
        SDL_FPoint leftPoints[4] = { topLeft, topPoint, basePoint, baseLeft };
        for (int i = 0; i < 4; i++) {
            leftVertices[i].position = leftPoints[i];
            leftVertices[i].color = { 0.24f, 0.24f, 0.24f, 0.78f };
            leftVertices[i].tex_coord = { 0.0f, 0.0f };
        }
        const int indices[6] = { 0, 1, 2, 0, 2, 3 };
        SDL_RenderGeometry(gRenderer, nullptr, leftVertices, 4, indices, 6);

        // Right side (lighter)
        SDL_SetRenderDrawColor(gRenderer, 100, 100, 100, 200);
        SDL_Vertex rightVertices[4];
        SDL_FPoint rightPoints[4] = { topRight, topPoint, basePoint, baseRight };
        for (int i = 0; i < 4; i++) {
            rightVertices[i].position = rightPoints[i];
            rightVertices[i].color = { 0.39f, 0.39f, 0.39f, 0.78f };
            rightVertices[i].tex_coord = { 0.0f, 0.0f };
        }
        SDL_RenderGeometry(gRenderer, nullptr, rightVertices, 4, indices, 6);
    }
}

void drawOverlay() {
    int windowW, windowH;
    SDL_GetWindowSize(gWindow, &windowW, &windowH);
    SDL_Color textColor = { 220, 220, 220, 255 };

    if (!gLevels.empty()) {
        std::string text = "Level: " + std::to_string(gCurrentLevelIndex + 1) + " / " + std::to_string(gLevels.size());
        int textW = 0, textH = 0;
        if (gFont && TTF_GetStringSizeWrapped(gFont, text.c_str(), 0, 0, &textW, &textH)) {
        }
        else {
            textW = (int)text.length() * 8;
        }

        SDL_SetRenderDrawColor(gRenderer, 20, 20, 20, 180);
        SDL_FRect bgRect = { 10, (float)windowH - 35, (float)textW + 10.0f, 25 };
        SDL_RenderFillRect(gRenderer, &bgRect);
        renderText(text, 15, (float)windowH - 32, textColor);
    }

    float uiWidth = 200;
    float uiX = (float)windowW - uiWidth - 10 - TILE_PANEL_WIDTH;
    SDL_SetRenderDrawColor(gRenderer, 20, 20, 20, 180);
    SDL_FRect bgRect = { uiX, 10, uiWidth, 35 };
    SDL_RenderFillRect(gRenderer, &bgRect);
    renderText("Grid Size:", uiX + 10, 18, textColor);

    int labelW = 0, labelH = 0;
    if (gFont && TTF_GetStringSizeWrapped(gFont, "Grid Size:", 0, 0, &labelW, &labelH)) {
    }
    else {
        labelW = 80;
    }

    SDL_FRect valueRect = { uiX + 10 + (float)labelW + 5, 15, 60, 25 };

    if (gIsEditingGridSize) {
        SDL_SetRenderDrawColor(gRenderer, 100, 150, 255, 255);
    }
    else {
        SDL_SetRenderDrawColor(gRenderer, 60, 60, 60, 255);
    }
    SDL_RenderFillRect(gRenderer, &valueRect);
    SDL_SetRenderDrawColor(gRenderer, 240, 240, 240, 255);
    SDL_RenderRect(gRenderer, &valueRect);

    std::string displayText = (gIsEditingGridSize ? gGridSizeInput : std::to_string(gMapSize)) + "x" + (gIsEditingGridSize ? gGridSizeInput : std::to_string(gMapSize));
    renderText(displayText, valueRect.x + 5, valueRect.y + 5, textColor);

    if (gIsEditingGridSize && SDL_GetTicks() % 1000 < 500) {
        int cursorW = 0, cursorH = 0;
        if (gFont && !gGridSizeInput.empty() && TTF_GetStringSizeWrapped(gFont, gGridSizeInput.c_str(), 0, 0, &cursorW, &cursorH)) {
        }
        else {
            cursorW = (int)gGridSizeInput.length() * 8;
        }
        float cursorX = valueRect.x + 5 + (float)cursorW;
        SDL_SetRenderDrawColor(gRenderer, 255, 255, 255, 255);
        SDL_RenderLine(gRenderer, cursorX, valueRect.y + 5, cursorX, valueRect.y + 20);
    }
}

void draw() {
    SDL_SetRenderDrawColor(gRenderer, 31, 41, 55, 255);
    SDL_RenderClear(gRenderer);
    if (!gLevels.empty()) {
        Level& currentLevel = gLevels[gCurrentLevelIndex];
        int mapSize = (int)currentLevel.map.size();
        float tileWidth, tileHeight;
        updateTileSizes(currentLevel.zoomLevel, tileWidth, tileHeight);
        for (int y = 0; y < mapSize; ++y) {
            for (int x = 0; x < mapSize; ++x) {
                drawTile(x, y, currentLevel.map[x][y], tileWidth, tileHeight, currentLevel.mapOffsetX, currentLevel.mapOffsetY);
            }
        }
    }
    drawTilePanel();
    drawOverlay();
    SDL_RenderPresent(gRenderer);
}

// --- Event Handling ---
void handleEvents(bool& quit_flag) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_EVENT_QUIT) {
            quit_flag = true;
            return;
        }

        if (gIsEditingGridSize) {
            if (event.type == SDL_EVENT_TEXT_INPUT) {
                char inputChar = event.text.text[0];
                if (inputChar >= '0' && inputChar <= '9') {
                    if (gGridSizeInput.length() < 2) {
                        gGridSizeInput += inputChar;
                    }
                }
            }
            else if (event.type == SDL_EVENT_KEY_DOWN) {
                if (event.key.key >= SDLK_0 && event.key.key <= SDLK_9) {
                    char digit = '0' + (event.key.key - SDLK_0);
                    if (gGridSizeInput.length() < 2) {
                        gGridSizeInput += digit;
                    }
                }
                else if (event.key.scancode == SDL_SCANCODE_BACKSPACE) {
                    if (!gGridSizeInput.empty()) {
                        gGridSizeInput.pop_back();
                    }
                }
                else if (event.key.scancode == SDL_SCANCODE_RETURN) {
                    try {
                        int newSize = gGridSizeInput.empty() ? gMapSize : std::stoi(gGridSizeInput);

                        if (newSize >= 5 && newSize <= 99) {
                            if (!gLevels.empty()) {
                                std::cout << "[Action] Grid size set to " << newSize << "x" << newSize << ". Resizing and resetting current level." << std::endl;
                                gLevels[gCurrentLevelIndex].map = generateMap(newSize);
                                gMapSize = newSize;
                                snapToNearestTile();
                            }
                        }
                        else {
                            gGridSizeInput = std::to_string(gMapSize);
                        }
                    }
                    catch (const std::exception&) {
                        gGridSizeInput = std::to_string(gMapSize);
                    }
                    gIsEditingGridSize = false;
                    SDL_StopTextInput(gWindow);
                }
                else if (event.key.scancode == SDL_SCANCODE_ESCAPE) {
                    gIsEditingGridSize = false;
                    SDL_StopTextInput(gWindow);
                    gGridSizeInput = std::to_string(gMapSize);
                }
            }
            else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
                int windowW, windowH;
                SDL_GetWindowSize(gWindow, &windowW, &windowH);
                float uiWidth = 200;
                float uiX = (float)windowW - uiWidth - 10 - TILE_PANEL_WIDTH;

                int labelW = 0, labelH = 0;
                if (gFont && TTF_GetStringSizeWrapped(gFont, "Grid Size:", 0, 0, &labelW, &labelH)) {
                }
                else {
                    labelW = 80;
                }

                SDL_FRect valueRect = { uiX + 10 + (float)labelW + 5, 15, 60, 25 };

                bool clickedInBox = (event.button.x >= valueRect.x && event.button.x <= valueRect.x + valueRect.w &&
                    event.button.y >= valueRect.y && event.button.y <= valueRect.y + valueRect.h);

                if (!clickedInBox) {
                    gIsEditingGridSize = false;
                    SDL_StopTextInput(gWindow);
                    gGridSizeInput = std::to_string(gMapSize);
                }
            }
            continue;
        }

        if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT) {
            int windowW, windowH;
            SDL_GetWindowSize(gWindow, &windowW, &windowH);

            // Check if click is in tile panel
            float panelX = (float)(windowW - TILE_PANEL_WIDTH);
            if (event.button.x >= panelX) {
                // Check scrollbar click
                float tileY = 50;
                float viewportHeight = windowH - tileY - 10;
                float tileSize = 60;
                float spacing = 8;
                int totalTiles = (int)gTileTextures.size();
                float contentHeight = totalTiles * (tileSize + spacing);

                if (contentHeight > viewportHeight) {
                    float scrollbarWidth = 8;
                    float scrollbarX = panelX + TILE_PANEL_WIDTH - scrollbarWidth - 5;
                    float scrollbarHeight = viewportHeight;
                    float thumbHeight = (viewportHeight / contentHeight) * scrollbarHeight;
                    thumbHeight = std::max(20.0f, thumbHeight);
                    float maxScroll = std::max(0.0f, contentHeight - viewportHeight);
                    float thumbY = tileY + (gTilePanelScroll / maxScroll) * (scrollbarHeight - thumbHeight);

                    if (event.button.x >= scrollbarX && event.button.x <= scrollbarX + scrollbarWidth &&
                        event.button.y >= thumbY && event.button.y <= thumbY + thumbHeight) {
                        gIsDraggingScrollbar = true;
                        gScrollbarDragOffset = event.button.y - thumbY;
                        continue;
                    }
                }

                // Handle tile panel clicks
                for (int i = 0; i < totalTiles; ++i) {
                    float itemY = tileY + (i * (tileSize + spacing)) - gTilePanelScroll;

                    if (itemY + tileSize < tileY || itemY > tileY + viewportHeight) continue;

                    if (event.button.y >= itemY && event.button.y <= itemY + tileSize) {
                        gSelectedTileType = i;
                        std::cout << "[Action] Selected tile ID: " << i << std::endl;
                        break;
                    }
                }
                continue;
            }

            // Handle grid size input box clicks
            float uiWidth = 200;
            float uiX = (float)windowW - uiWidth - 10 - TILE_PANEL_WIDTH;

            int labelW = 0, labelH = 0;
            if (gFont && TTF_GetStringSizeWrapped(gFont, "Grid Size:", 0, 0, &labelW, &labelH)) {
            }
            else {
                labelW = 80;
            }

            SDL_FRect valueRect = { uiX + 10 + (float)labelW + 5, 15, 60, 25 };

            if (event.button.x >= valueRect.x && event.button.x <= valueRect.x + valueRect.w &&
                event.button.y >= valueRect.y && event.button.y <= valueRect.y + valueRect.h) {
                if (!gIsEditingGridSize) {
                    gIsEditingGridSize = true;
                    gGridSizeInput = "";
                    SDL_StartTextInput(gWindow);
                }
                continue;
            }
        }

        if (gLevels.empty()) {
            if (event.type == SDL_EVENT_KEY_DOWN && event.key.scancode == SDL_SCANCODE_N) {
                addNewLevel();
                snapToNearestTile();
            }
            continue;
        }

        Level& currentLevel = gLevels[gCurrentLevelIndex];
        int mapSize = (int)currentLevel.map.size();
        switch (event.type) {
        case SDL_EVENT_KEY_DOWN:
            switch (event.key.scancode) {
            case SDL_SCANCODE_S: saveLevels(); break;
            case SDL_SCANCODE_RIGHT:
                if (gCurrentLevelIndex < gLevels.size() - 1) {
                    gCurrentLevelIndex++;
                    gMapSize = (int)gLevels[gCurrentLevelIndex].map.size();
                    gGridSizeInput = std::to_string(gMapSize);
                    std::cout << "[Action] Switched to Level " << gCurrentLevelIndex + 1 << " (" << gMapSize << "x" << gMapSize << ")." << std::endl;
                    snapToNearestTile();
                }
                break;
            case SDL_SCANCODE_LEFT:
                if (gCurrentLevelIndex > 0) {
                    gCurrentLevelIndex--;
                    gMapSize = (int)gLevels[gCurrentLevelIndex].map.size();
                    gGridSizeInput = std::to_string(gMapSize);
                    std::cout << "[Action] Switched to Level " << gCurrentLevelIndex + 1 << " (" << gMapSize << "x" << gMapSize << ")." << std::endl;
                    snapToNearestTile();
                }
                break;
            case SDL_SCANCODE_N: addNewLevel(); snapToNearestTile(); break;
            case SDL_SCANCODE_DELETE: deleteCurrentLevel(); snapToNearestTile(); break;
            case SDL_SCANCODE_R:
            {
                int currentSize = (int)gLevels[gCurrentLevelIndex].map.size();
                gLevels[gCurrentLevelIndex].map = generateMap(currentSize);
                snapToNearestTile();
                std::cout << "[Status] Level " << gCurrentLevelIndex + 1 << " has been reset to a flat grid." << std::endl;
            }
            break;
            default: break;
            }
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            if (event.button.button == SDL_BUTTON_LEFT) {
                int windowW, windowH;
                SDL_GetWindowSize(gWindow, &windowW, &windowH);
                float panelX = (float)(windowW - TILE_PANEL_WIDTH);

                // Check if click is in tile panel - if so, don't start dragging
                if (event.button.x >= panelX) {
                    break;
                }

                gIsDragging = false; // Start as false, will become true if moved enough
                gIsSnapping = false;
                gLastMouseX = (float)event.button.x;
                gLastMouseY = (float)event.button.y;
                gMouseDownX = (float)event.button.x;
                gMouseDownY = (float)event.button.y;
            }
            break;
        case SDL_EVENT_MOUSE_BUTTON_UP:
            if (event.button.button == SDL_BUTTON_LEFT) {
                gIsDraggingScrollbar = false;
                bool wasDragging = gIsDragging;
                gIsDragging = false;

                std::cout << "[Debug] Mouse up - wasDragging: " << wasDragging << std::endl;

                if (!wasDragging) {
                    int windowW, windowH;
                    SDL_GetWindowSize(gWindow, &windowW, &windowH);

                    float panelX = (float)(windowW - TILE_PANEL_WIDTH);
                    if (event.button.x >= panelX) {
                        std::cout << "[Debug] Click in panel area, ignoring" << std::endl;
                        break;
                    }

                    float tileWidth, tileHeight;
                    updateTileSizes(currentLevel.zoomLevel, tileWidth, tileHeight);
                    SDL_FPoint cart = screenToCartesian((float)event.button.x, (float)event.button.y, tileWidth, tileHeight, currentLevel.mapOffsetX, currentLevel.mapOffsetY);
                    int mapX = (int)std::round(cart.x);
                    int mapY = (int)std::round(cart.y);

                    std::cout << "[Debug] Click at screen (" << event.button.x << ", " << event.button.y << ") -> map (" << mapX << ", " << mapY << ")" << std::endl;
                    std::cout << "[Debug] Map size: " << mapSize << ", Selected tile: " << gSelectedTileType << std::endl;

                    if (mapX >= 0 && mapX < mapSize && mapY >= 0 && mapY < mapSize) {
                        if (SDL_GetModState() & SDL_KMOD_LCTRL) {
                            // Lower tile height
                            currentLevel.map[mapX][mapY].height = std::max(0, currentLevel.map[mapX][mapY].height - 1);
                            std::cout << "[Action] Lowered height at (" << mapX << ", " << mapY << ") to " << currentLevel.map[mapX][mapY].height << std::endl;
                        }
                        else if (SDL_GetModState() & SDL_KMOD_LALT) {
                            // Raise tile height
                            currentLevel.map[mapX][mapY].height = std::min(10, currentLevel.map[mapX][mapY].height + 1);
                            std::cout << "[Action] Raised height at (" << mapX << ", " << mapY << ") to " << currentLevel.map[mapX][mapY].height << std::endl;
                        }
                        else {
                            // Apply selected tile type (default action)
                            int oldTileID = currentLevel.map[mapX][mapY].tileID;
                            currentLevel.map[mapX][mapY].tileID = gSelectedTileType;
                            std::cout << "[Action] Applied Tile " << gSelectedTileType << " at (" << mapX << ", " << mapY << ") - was: " << oldTileID << std::endl;
                        }
                    }
                    else {
                        std::cout << "[Debug] Click outside map bounds" << std::endl;
                    }
                }
            }
            break;
        case SDL_EVENT_MOUSE_MOTION:
            if (gIsDraggingScrollbar) {
                int windowW, windowH;
                SDL_GetWindowSize(gWindow, &windowW, &windowH);
                float panelX = (float)(windowW - TILE_PANEL_WIDTH);
                float tileY = 50;
                float viewportHeight = windowH - tileY - 10;
                float tileSize = 60;
                float spacing = 8;
                int totalTiles = (int)gTileTextures.size();
                float contentHeight = totalTiles * (tileSize + spacing);
                float scrollbarHeight = viewportHeight;
                float thumbHeight = (viewportHeight / contentHeight) * scrollbarHeight;
                thumbHeight = std::max(20.0f, thumbHeight);
                float maxScroll = std::max(0.0f, contentHeight - viewportHeight);

                float newThumbY = event.motion.y - gScrollbarDragOffset;
                float scrollRatio = (newThumbY - tileY) / (scrollbarHeight - thumbHeight);
                gTilePanelScroll = scrollRatio * maxScroll;
                gTilePanelScroll = std::max(0.0f, std::min(maxScroll, gTilePanelScroll));
            }
            else if (SDL_GetMouseState(nullptr, nullptr) & SDL_BUTTON_LMASK) {
                // Mouse button is held down
                float deltaX = event.motion.x - gMouseDownX;
                float deltaY = event.motion.y - gMouseDownY;
                float distance = std::sqrt(deltaX * deltaX + deltaY * deltaY);

                // Only start dragging if moved beyond threshold
                if (distance > DRAG_THRESHOLD) {
                    gIsDragging = true;
                }

                if (gIsDragging) {
                    currentLevel.mapOffsetX += (float)event.motion.x - gLastMouseX;
                    currentLevel.mapOffsetY += (float)event.motion.y - gLastMouseY;
                    currentLevel.targetOffsetX = currentLevel.mapOffsetX;
                    currentLevel.targetOffsetY = currentLevel.mapOffsetY;
                }

                gLastMouseX = (float)event.motion.x;
                gLastMouseY = (float)event.motion.y;
            }
            break;
        case SDL_EVENT_MOUSE_WHEEL:
            // Check if mouse is over tile panel for scrolling
        {
            float mouseX, mouseY;
            SDL_GetMouseState(&mouseX, &mouseY);
            int windowW, windowH;
            SDL_GetWindowSize(gWindow, &windowW, &windowH);
            float panelX = (float)(windowW - TILE_PANEL_WIDTH);

            if (mouseX >= panelX) {
                // Scroll the tile panel
                gTilePanelScroll -= event.wheel.y * 30.0f;
                float tileY = 50;
                float viewportHeight = windowH - tileY - 10;
                float tileSize = 60;
                float spacing = 8;
                int totalTiles = (int)gTileTextures.size();
                float contentHeight = totalTiles * (tileSize + spacing);
                float maxScroll = std::max(0.0f, contentHeight - viewportHeight);
                gTilePanelScroll = std::max(0.0f, std::min(maxScroll, gTilePanelScroll));
            }
            else {
                // Original zoom behavior for map
                float preZoomTileWidth, preZoomTileHeight;
                updateTileSizes(currentLevel.zoomLevel, preZoomTileWidth, preZoomTileHeight);
                SDL_FPoint worldPos = screenToCartesian(mouseX, mouseY, preZoomTileWidth, preZoomTileHeight, currentLevel.mapOffsetX, currentLevel.mapOffsetY);
                currentLevel.zoomLevel *= (event.wheel.y > 0 ? 1.1f : 0.9f);
                currentLevel.zoomLevel = std::max(0.2f, std::min(3.0f, currentLevel.zoomLevel));
                float postZoomTileWidth, postZoomTileHeight;
                updateTileSizes(currentLevel.zoomLevel, postZoomTileWidth, postZoomTileHeight);
                SDL_FPoint screenPosAfterZoom = cartesianToScreen(worldPos.x, worldPos.y, postZoomTileWidth, postZoomTileHeight, currentLevel.mapOffsetX, currentLevel.mapOffsetY);
                currentLevel.mapOffsetX += mouseX - screenPosAfterZoom.x;
                currentLevel.mapOffsetY += mouseY - screenPosAfterZoom.y;
                currentLevel.targetOffsetX = currentLevel.mapOffsetX;
                currentLevel.targetOffsetY = currentLevel.mapOffsetY;
                gIsSnapping = false;
            }
        }
        break;
        }
    }
}

// --- Main Loop ---
int main(int argc, char* args[]) {
    if (!init()) return 1;
    if (!loadLevels()) addInitialLevels(1);
    snapToNearestTile();
    bool quit = false;
    while (!quit) {
        handleEvents(quit);
        updateCamera();
        draw();
    }
    close();
    return 0;
}