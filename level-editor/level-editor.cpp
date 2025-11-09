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
int gMapSize = 25;
const float BASE_TILE_WIDTH = 32.0f;
const int NUM_LAYERS = 3;  // Fixed: Terrain, Player, Furniture

const std::string SAVE_PATH = "D:\\codingStudy\\IMED MCA\\Sem - 3\\Project\\level-editor-SDL3-osiris\\assets\\levels\\";
const std::string ASSET_PATH = "D:\\codingStudy\\IMED MCA\\Sem - 3\\Project\\level-editor-SDL3-osiris\\assets\\isometric tileset\\separated images\\";

// Layer names
const std::string LAYER_NAMES[NUM_LAYERS] = { "Terrain", "Player", "Furniture" };

// --- Data Structures ---
struct TileData {
    int tileID;  // Asset ID only
};

struct TileType {
    std::string name;
    int tileID;
    SDL_Texture* texture;
};

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

struct Layer {
    std::vector<std::vector<TileData>> map;
    bool visible = true;
    std::string name;
};

struct Level {
    Layer layers[NUM_LAYERS];  // Fixed 3 layers
    int activeLayer = 0;
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
const float DRAG_THRESHOLD = 5.0f;

bool gIsEditingGridSize = false;
std::string gGridSizeInput = "25";

std::vector<SDL_Texture*> gTileTextures;

float gTilePanelScroll = 0.0f;
bool gIsDraggingScrollbar = false;
float gScrollbarDragOffset = 0.0f;

// Forward declarations
bool init();
void close();
void handleEvents(bool& quit);
void updateCamera();
void draw();
bool loadLevels();
bool saveLevels();
void snapToNearestTile();

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
    for (auto& tileType : gTileTypes) {
        std::string path = ASSET_PATH + "tile_" +
            std::string(3 - std::min(3, (int)std::to_string(tileType.tileID).length()), '0') +
            std::to_string(tileType.tileID) + ".png";
        tileType.texture = loadTexture(path);
        if (!tileType.texture) {
            std::cerr << "Warning: Could not load texture for " << tileType.name << std::endl;
        }
    }

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

std::vector<std::vector<TileData>> generateMap(int mapSize, int defaultTileID) {
    std::vector<std::vector<TileData>> map;
    map.resize(mapSize, std::vector<TileData>(mapSize));
    for (int x = 0; x < mapSize; ++x) {
        for (int y = 0; y < mapSize; ++y) {
            map[x][y].tileID = defaultTileID;
        }
    }
    return map;
}

Layer createLayer(int mapSize, const std::string& name, int layerIndex) {
    Layer layer;
    layer.name = name;
    layer.visible = true;

    // Set default tile IDs based on layer type
    if (layerIndex == 0) {
        // Terrain layer: default to tile 1 (debug texture)
        layer.map = generateMap(mapSize, 1);
    }
    else if (layerIndex == 1) {
        // Player layer: default to 0, set (0,0) to 3
        layer.map = generateMap(mapSize, 0);
        layer.map[0][0].tileID = 3;
    }
    else {
        // Furniture layer: default to 0
        layer.map = generateMap(mapSize, 0);
    }

    return layer;
}

// --- Level State Management ---
void addInitialLevels(int count) {
    if (gLevels.empty()) {
        for (int i = 0; i < count; ++i) {
            Level level;
            for (int j = 0; j < NUM_LAYERS; ++j) {
                level.layers[j] = createLayer(gMapSize, LAYER_NAMES[j], j);
            }
            level.activeLayer = 0;
            gLevels.push_back(level);
        }
    }
}

void addNewLevel() {
    Level level;
    for (int j = 0; j < NUM_LAYERS; ++j) {
        level.layers[j] = createLayer(gMapSize, LAYER_NAMES[j], j);
    }
    level.activeLayer = 0;
    gLevels.push_back(level);
    gCurrentLevelIndex = gLevels.size() - 1;
    std::cout << "[Status] New level created with " << NUM_LAYERS << " layers (" << gMapSize << "x" << gMapSize << ")." << std::endl;
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

// --- Serialization (Separate CSV files per layer) ---
bool saveLevels() {
    std::cout << "[Action] Saving all levels to separate layer files..." << std::endl;

    for (size_t i = 0; i < gLevels.size(); ++i) {
        const auto& level = gLevels[i];
        int mapSize = (int)level.layers[0].map.size();

        // Save metadata file (camera position, zoom, etc.)
        std::string metaFilename = SAVE_PATH + "level_" + std::to_string(i + 1) + "_meta.csv";
        std::ofstream metaFile(metaFilename);
        if (metaFile.is_open()) {
            metaFile << "zoomLevel,mapOffsetX,mapOffsetY,mapSize,activeLayer\n";
            metaFile << level.zoomLevel << "," << level.mapOffsetX << "," << level.mapOffsetY << ","
                << mapSize << "," << level.activeLayer << "\n";
            metaFile.close();
        }

        // Save each layer to separate file
        const char* layerSuffix[NUM_LAYERS] = { "terrain", "player", "furniture" };

        for (int layerIdx = 0; layerIdx < NUM_LAYERS; ++layerIdx) {
            std::string filename = SAVE_PATH + "level_" + std::to_string(i + 1) + "_" +
                layerSuffix[layerIdx] + ".csv";
            std::ofstream file(filename);

            if (!file.is_open()) {
                std::cerr << "Error: Could not save " << filename << std::endl;
                continue;
            }

            const auto& layer = level.layers[layerIdx];

            // Save tile ID data
            for (int y = 0; y < mapSize; ++y) {
                for (int x = 0; x < mapSize; ++x) {
                    file << layer.map[x][y].tileID;
                    if (x < mapSize - 1) file << ",";
                }
                file << "\n";
            }

            file.close();
        }
    }

    // Clean up orphan files
    size_t levelIndexToDelete = gLevels.size() + 1;
    const char* suffixes[] = { "terrain", "player", "furniture", "meta" };
    while (true) {
        bool foundAny = false;
        for (const char* suffix : suffixes) {
            std::string filename = SAVE_PATH + "level_" + std::to_string(levelIndexToDelete) + "_" + suffix + ".csv";
            if (std::ifstream(filename).good()) {
                if (std::remove(filename.c_str()) == 0) {
                    std::cout << "[Status] Cleaned up orphan file: " << filename << std::endl;
                }
                foundAny = true;
            }
        }
        if (!foundAny) break;
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
        std::string metaFilename = SAVE_PATH + "level_" + std::to_string(levelIndex) + "_meta.csv";
        std::ifstream metaFile(metaFilename);

        if (!metaFile.is_open()) {
            break;
        }

        try {
            Level level;
            std::string line, value;

            // Read metadata (skip header line if present)
            if (std::getline(metaFile, line)) {
                // Check if first line is header
                if (line.find("zoomLevel") != std::string::npos) {
                    std::getline(metaFile, line); // Read actual data line
                }

                std::stringstream ss(line);
                if (std::getline(ss, value, ',')) level.zoomLevel = std::stof(value);
                if (std::getline(ss, value, ',')) level.mapOffsetX = std::stof(value);
                if (std::getline(ss, value, ',')) level.mapOffsetY = std::stof(value);

                int mapSize = gMapSize;
                if (std::getline(ss, value, ',')) mapSize = std::stoi(value);

                if (std::getline(ss, value, ',')) level.activeLayer = std::stoi(value);

                level.targetOffsetX = level.mapOffsetX;
                level.targetOffsetY = level.mapOffsetY;

                // Load each layer from separate files
                const char* layerSuffix[NUM_LAYERS] = { "terrain", "player", "furniture" };

                for (int layerIdx = 0; layerIdx < NUM_LAYERS; ++layerIdx) {
                    std::string layerFilename = SAVE_PATH + "level_" + std::to_string(levelIndex) + "_" +
                        layerSuffix[layerIdx] + ".csv";
                    std::ifstream layerFile(layerFilename);

                    Layer layer;
                    layer.name = LAYER_NAMES[layerIdx];
                    layer.visible = true;
                    layer.map = generateMap(mapSize, 0);

                    if (layerFile.is_open()) {
                        for (int y = 0; y < mapSize; ++y) {
                            if (!std::getline(layerFile, line)) break;
                            std::stringstream rowStream(line);
                            for (int x = 0; x < mapSize; ++x) {
                                if (!std::getline(rowStream, value, ',')) break;
                                layer.map[x][y].tileID = std::stoi(value);
                            }
                        }
                        layerFile.close();
                    }
                    else {
                        std::cerr << "Warning: Could not load " << layerFilename << std::endl;
                    }

                    level.layers[layerIdx] = layer;
                }

                gLevels.push_back(level);
            }
        }
        catch (const std::exception& e) {
            std::cerr << "Error loading level " << levelIndex << ": " << e.what() << std::endl;
        }

        metaFile.close();
        levelIndex++;
    }

    if (!gLevels.empty()) {
        gCurrentLevelIndex = 0;
        if (!gLevels[0].layers[0].map.empty()) {
            gMapSize = (int)gLevels[0].layers[0].map.size();
            gGridSizeInput = std::to_string(gMapSize);
        }
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

    int mapSize = (int)currentLevel.layers[0].map.size();
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
    if (std::abs(currentLevel.targetOffsetX - currentLevel.mapOffsetX) < 1.0f &&
        std::abs(currentLevel.targetOffsetY - currentLevel.mapOffsetY) < 1.0f) {
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

    SDL_SetRenderDrawColor(gRenderer, 40, 40, 40, 255);
    SDL_FRect panelBg = { panelX, 0, (float)TILE_PANEL_WIDTH, (float)windowH };
    SDL_RenderFillRect(gRenderer, &panelBg);

    SDL_SetRenderDrawColor(gRenderer, 80, 80, 80, 255);
    SDL_RenderRect(gRenderer, &panelBg);

    SDL_Color titleColor = { 220, 220, 220, 255 };
    renderText("All Tiles", panelX + 10, 15, titleColor);

    float tileY = 50;
    float tileSize = 60;
    float spacing = 8;
    int totalTiles = (int)gTileTextures.size();
    float contentHeight = totalTiles * (tileSize + spacing);
    float viewportHeight = windowH - tileY - 10;

    float maxScroll = std::max(0.0f, contentHeight - viewportHeight);
    gTilePanelScroll = std::max(0.0f, std::min(maxScroll, gTilePanelScroll));

    SDL_Rect clipRect = { (int)panelX, (int)tileY, TILE_PANEL_WIDTH, (int)viewportHeight };
    SDL_SetRenderClipRect(gRenderer, &clipRect);

    for (int i = 0; i < totalTiles; ++i) {
        float itemX = panelX + 10;
        float itemY = tileY + (i * (tileSize + spacing)) - gTilePanelScroll;

        if (itemY + tileSize < tileY || itemY > tileY + viewportHeight) continue;

        if (i == gSelectedTileType) {
            SDL_SetRenderDrawColor(gRenderer, 100, 150, 255, 255);
            SDL_FRect highlightRect = { itemX - 5, itemY - 3, (float)TILE_PANEL_WIDTH - 20, tileSize + 6 };
            SDL_RenderFillRect(gRenderer, &highlightRect);
        }

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

        SDL_Color textColor = { 220, 220, 220, 255 };
        std::string tileLabel = "Tile " + std::to_string(i);
        renderText(tileLabel, itemX + 65, itemY + 22, textColor);
    }

    SDL_SetRenderClipRect(gRenderer, nullptr);

    if (contentHeight > viewportHeight) {
        float scrollbarWidth = 8;
        float scrollbarX = panelX + TILE_PANEL_WIDTH - scrollbarWidth - 5;
        float scrollbarY = tileY;
        float scrollbarHeight = viewportHeight;

        SDL_SetRenderDrawColor(gRenderer, 60, 60, 60, 255);
        SDL_FRect trackRect = { scrollbarX, scrollbarY, scrollbarWidth, scrollbarHeight };
        SDL_RenderFillRect(gRenderer, &trackRect);

        float thumbHeight = (viewportHeight / contentHeight) * scrollbarHeight;
        thumbHeight = std::max(20.0f, thumbHeight);
        float thumbY = scrollbarY + (gTilePanelScroll / maxScroll) * (scrollbarHeight - thumbHeight);

        SDL_SetRenderDrawColor(gRenderer, 120, 120, 120, 255);
        SDL_FRect thumbRect = { scrollbarX, thumbY, scrollbarWidth, thumbHeight };
        SDL_RenderFillRect(gRenderer, &thumbRect);
    }
}

void drawTile(int x, int y, const TileData& data, float tileWidth, float tileHeight, float mapOffsetX, float mapOffsetY, int alpha = 255) {
    SDL_FPoint screenPos = cartesianToScreen((float)x, (float)y, tileWidth, tileHeight, mapOffsetX, mapOffsetY);
    float px = screenPos.x;
    float py = screenPos.y;

    int windowW, windowH;
    SDL_GetWindowSize(gWindow, &windowW, &windowH);
    if (px + tileWidth < 0 || px - tileWidth > windowW || py + tileHeight < 0 || py - tileHeight * 3 > windowH) {
        return;
    }

    SDL_Texture* tileTexture = nullptr;
    if (data.tileID >= 0 && data.tileID < (int)gTileTextures.size()) {
        tileTexture = gTileTextures[data.tileID];
    }

    if (tileTexture) {
        SDL_SetTextureAlphaMod(tileTexture, alpha);
        SDL_FRect destRect = {
            px - tileWidth / 2.0f,
            py - tileHeight / 2.0f,
            tileWidth,
            tileHeight
        };
        SDL_RenderTexture(gRenderer, tileTexture, nullptr, &destRect);
        SDL_SetTextureAlphaMod(tileTexture, 255);
    }
}

void drawMap() {
    if (gLevels.empty()) return;
    Level& currentLevel = gLevels[gCurrentLevelIndex];

    int mapSize = (int)currentLevel.layers[0].map.size();
    float tileWidth, tileHeight;
    updateTileSizes(currentLevel.zoomLevel, tileWidth, tileHeight);

    // Draw all visible layers from bottom to top
    for (int layerIdx = 0; layerIdx < NUM_LAYERS; ++layerIdx) {
        const Layer& layer = currentLevel.layers[layerIdx];

        if (!layer.visible) continue;

        // Determine alpha for inactive layers
        int alpha = (layerIdx == currentLevel.activeLayer) ? 255 : 128;

        for (int y = 0; y < mapSize; ++y) {
            for (int x = 0; x < mapSize; ++x) {
                const TileData& data = layer.map[x][y];

                // Skip rendering tiles with ID 0 on player and furniture layers
                if ((layerIdx == 1 || layerIdx == 2) && data.tileID == 0) {
                    continue;
                }

                drawTile(x, y, data, tileWidth, tileHeight,
                    currentLevel.mapOffsetX, currentLevel.mapOffsetY, alpha);
            }
        }
    }
}

void drawOverlay() {
    int windowW, windowH;
    SDL_GetWindowSize(gWindow, &windowW, &windowH);
    SDL_Color textColor = { 220, 220, 220, 255 };

    if (!gLevels.empty()) {
        Level& currentLevel = gLevels[gCurrentLevelIndex];

        // Level counter
        std::string levelText = "Level: " + std::to_string(gCurrentLevelIndex + 1) + " / " + std::to_string(gLevels.size());
        int textW = 0, textH = 0;
        if (gFont && TTF_GetStringSizeWrapped(gFont, levelText.c_str(), 0, 0, &textW, &textH)) {
        }
        else {
            textW = (int)levelText.length() * 8;
        }

        SDL_SetRenderDrawColor(gRenderer, 20, 20, 20, 180);
        SDL_FRect bgRect = { 10, (float)windowH - 35, (float)textW + 10.0f, 25 };
        SDL_RenderFillRect(gRenderer, &bgRect);
        renderText(levelText, 15, (float)windowH - 32, textColor);

        // Layer indicator with visibility info
        std::string layerText = "Layer: " + currentLevel.layers[currentLevel.activeLayer].name +
            " (" + std::to_string(currentLevel.activeLayer + 1) + "/" + std::to_string(NUM_LAYERS) + ")";

        // Add visibility indicators for all layers
        layerText += " | Visible: ";
        for (int i = 0; i < NUM_LAYERS; ++i) {
            if (currentLevel.layers[i].visible) {
                layerText += LAYER_NAMES[i][0]; // First letter of layer name
            }
        }

        int layerTextW = 0;
        if (gFont && TTF_GetStringSizeWrapped(gFont, layerText.c_str(), 0, 0, &layerTextW, &textH)) {
        }
        else {
            layerTextW = (int)layerText.length() * 8;
        }

        SDL_SetRenderDrawColor(gRenderer, 20, 20, 20, 180);
        SDL_FRect layerBgRect = { 10, (float)windowH - 65, (float)layerTextW + 10.0f, 25 };
        SDL_RenderFillRect(gRenderer, &layerBgRect);

        SDL_Color layerColor = { 100, 200, 255, 255 };
        renderText(layerText, 15, (float)windowH - 62, layerColor);
    }

    // Grid size UI
    float uiWidth = 200;
    float uiX = (float)windowW - uiWidth - 10 - TILE_PANEL_WIDTH;
    SDL_SetRenderDrawColor(gRenderer, 20, 20, 20, 180);
    SDL_FRect bgRect2 = { uiX, 10, uiWidth, 35 };
    SDL_RenderFillRect(gRenderer, &bgRect2);
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

    std::string displayText = (gIsEditingGridSize && !gGridSizeInput.empty() ? gGridSizeInput : std::to_string(gMapSize)) + "x" +
        (gIsEditingGridSize && !gGridSizeInput.empty() ? gGridSizeInput : std::to_string(gMapSize));
    renderText(displayText, valueRect.x + 5, valueRect.y + 5, textColor);

    if (gIsEditingGridSize && SDL_GetTicks() % 1000 < 500) {
        std::string cursorText = gGridSizeInput.empty() ? "" : gGridSizeInput;
        int cursorW = 0, cursorH = 0;
        if (gFont && !cursorText.empty() && TTF_GetStringSizeWrapped(gFont, cursorText.c_str(), 0, 0, &cursorW, &cursorH)) {
        }
        else {
            cursorW = (int)cursorText.length() * 8;
        }
        float cursorX = valueRect.x + 5 + (float)cursorW;
        SDL_SetRenderDrawColor(gRenderer, 255, 255, 255, 255);
        SDL_RenderLine(gRenderer, cursorX, valueRect.y + 5, cursorX, valueRect.y + 20);
    }
}

void draw() {
    SDL_SetRenderDrawColor(gRenderer, 30, 30, 30, 255);
    SDL_RenderClear(gRenderer);

    drawMap();
    drawTilePanel();
    drawOverlay();

    SDL_RenderPresent(gRenderer);
}

// --- Input Handling ---
void handleEvents(bool& quit) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_EVENT_QUIT) {
            quit = true;
        }
        else if (e.type == SDL_EVENT_KEY_DOWN) {
            if (gIsEditingGridSize) {
                if (e.key.key == SDLK_RETURN || e.key.key == SDLK_KP_ENTER) {
                    std::cout << "[DEBUG] Input string: '" << gGridSizeInput << "'" << std::endl;
                    std::cout << "[DEBUG] gMapSize before: " << gMapSize << std::endl;
                    if (!gGridSizeInput.empty()) {
                        try {
                            int newSize = std::stoi(gGridSizeInput);
                            std::cout << "[DEBUG] newSize parsed: " << newSize << std::endl;
                            if (newSize >= 5 && newSize <= 100) {
                                gMapSize = newSize;
                                std::cout << "[DEBUG] gMapSize after assignment: " << gMapSize << std::endl;
                                std::cout << "[Status] Grid size changed to " << gMapSize << "x" << gMapSize << std::endl;
                                if (!gLevels.empty()) {
                                    Level& currentLevel = gLevels[gCurrentLevelIndex];
                                    for (int i = 0; i < NUM_LAYERS; ++i) {
                                        currentLevel.layers[i] = createLayer(gMapSize, LAYER_NAMES[i], i);
                                    }
                                }
                            }
                            else {
                                std::cout << "[Error] Grid size must be between 5 and 100" << std::endl;
                            }
                        }
                        catch (const std::exception& e) {
                            std::cout << "[Error] Invalid grid size input: " << e.what() << std::endl;
                        }
                    }
                    gIsEditingGridSize = false;
                    SDL_StopTextInput(gWindow);  // Disable text input
                }
                else if (e.key.key == SDLK_ESCAPE) {
                    gGridSizeInput = std::to_string(gMapSize);
                    gIsEditingGridSize = false;
                    SDL_StopTextInput(gWindow);  // Disable text input
                }
                else if (e.key.key == SDLK_BACKSPACE && !gGridSizeInput.empty()) {
                    gGridSizeInput.pop_back();
                }
            }
            else {
                // Normal editor controls
                if (e.key.key == SDLK_S && (e.key.mod & SDL_KMOD_CTRL)) {
                    saveLevels();
                }
                else if (e.key.key == SDLK_N && (e.key.mod & SDL_KMOD_CTRL)) {
                    addNewLevel();
                }
                else if (e.key.key == SDLK_D && (e.key.mod & SDL_KMOD_CTRL)) {
                    deleteCurrentLevel();
                }
                else if (e.key.key == SDLK_LEFT) {
                    if (gCurrentLevelIndex > 0) {
                        gCurrentLevelIndex--;
                        std::cout << "[Status] Switched to Level " << gCurrentLevelIndex + 1 << std::endl;
                    }
                }
                else if (e.key.key == SDLK_RIGHT) {
                    if (gCurrentLevelIndex < gLevels.size() - 1) {
                        gCurrentLevelIndex++;
                        std::cout << "[Status] Switched to Level " << gCurrentLevelIndex + 1 << std::endl;
                    }
                }
                else if (e.key.key == SDLK_TAB) {
                    // Cycle through layers
                    if (!gLevels.empty()) {
                        Level& currentLevel = gLevels[gCurrentLevelIndex];
                        currentLevel.activeLayer = (currentLevel.activeLayer + 1) % NUM_LAYERS;
                        std::cout << "[Status] Switched to " << currentLevel.layers[currentLevel.activeLayer].name << " layer" << std::endl;
                    }
                }
                else if (e.key.key == SDLK_V) {
                    // Toggle visibility of current layer
                    if (!gLevels.empty()) {
                        Level& currentLevel = gLevels[gCurrentLevelIndex];
                        currentLevel.layers[currentLevel.activeLayer].visible =
                            !currentLevel.layers[currentLevel.activeLayer].visible;
                        std::cout << "[Status] " << currentLevel.layers[currentLevel.activeLayer].name
                            << " layer " << (currentLevel.layers[currentLevel.activeLayer].visible ? "shown" : "hidden") << std::endl;
                    }
                }
                else if (e.key.key >= SDLK_1 && e.key.key <= SDLK_3) {
                    // Quick layer switch with number keys
                    if (!gLevels.empty()) {
                        int layerNum = e.key.key - SDLK_1;
                        if (layerNum < NUM_LAYERS) {
                            gLevels[gCurrentLevelIndex].activeLayer = layerNum;
                            std::cout << "[Status] Switched to " << LAYER_NAMES[layerNum] << " layer" << std::endl;
                        }
                    }
                }
                else if (e.key.key == SDLK_SPACE) {
                    snapToNearestTile();
                }
            }
        }
        else if (e.type == SDL_EVENT_TEXT_INPUT) {
            if (gIsEditingGridSize) {
                for (int i = 0; e.text.text[i] != '\0'; ++i) {
                    if (e.text.text[i] >= '0' && e.text.text[i] <= '9') {
                        if (gGridSizeInput.length() < 3) {
                            gGridSizeInput += e.text.text[i];
                        }
                    }
                }
            }
        }
        else if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
            if (e.button.button == SDL_BUTTON_LEFT) {
                int windowW, windowH;
                SDL_GetWindowSize(gWindow, &windowW, &windowH);
                float panelX = (float)(windowW - TILE_PANEL_WIDTH);

                // Check if clicking on grid size input
                float uiWidth = 200;
                float uiX = (float)windowW - uiWidth - 10 - TILE_PANEL_WIDTH;
                int labelW = 80;
                SDL_FRect valueRect = { uiX + 10 + (float)labelW + 5, 15, 60, 25 };

                if (e.button.x >= valueRect.x && e.button.x <= valueRect.x + valueRect.w &&
                    e.button.y >= valueRect.y && e.button.y <= valueRect.y + valueRect.h) {
                    gIsEditingGridSize = true;
                    gGridSizeInput = "";  // Clear input for new entry
                    SDL_StartTextInput(gWindow);  // Enable text input
                }
                else if (e.button.x >= panelX) {
                    // Tile panel interaction
                    float tileY = 50;
                    float tileSize = 60;
                    float spacing = 8;

                    int totalTiles = (int)gTileTextures.size();
                    float viewportHeight = windowH - tileY - 10;
                    float contentHeight = totalTiles * (tileSize + spacing);

                    if (contentHeight > viewportHeight) {
                        float scrollbarWidth = 8;
                        float scrollbarX = panelX + TILE_PANEL_WIDTH - scrollbarWidth - 5;

                        if (e.button.x >= scrollbarX && e.button.x <= scrollbarX + scrollbarWidth &&
                            e.button.y >= tileY && e.button.y <= tileY + viewportHeight) {
                            gIsDraggingScrollbar = true;
                            float maxScroll = std::max(0.0f, contentHeight - viewportHeight);
                            float thumbHeight = (viewportHeight / contentHeight) * viewportHeight;
                            thumbHeight = std::max(20.0f, thumbHeight);
                            float thumbY = tileY + (gTilePanelScroll / maxScroll) * (viewportHeight - thumbHeight);
                            gScrollbarDragOffset = e.button.y - thumbY;
                        }
                    }

                    if (!gIsDraggingScrollbar && e.button.y >= tileY) {
                        for (int i = 0; i < totalTiles; ++i) {
                            float itemY = tileY + (i * (tileSize + spacing)) - gTilePanelScroll;
                            if (e.button.y >= itemY && e.button.y <= itemY + tileSize) {
                                gSelectedTileType = i;
                                std::cout << "[Status] Selected tile " << i << std::endl;
                                break;
                            }
                        }
                    }
                }
                else {
                    gIsEditingGridSize = false;
                    SDL_StopTextInput(gWindow);  // Disable text input when clicking outside
                    // Map interaction - paint tile
                    gMouseDownX = (float)e.button.x;
                    gMouseDownY = (float)e.button.y;

                    if (!gLevels.empty()) {
                        Level& currentLevel = gLevels[gCurrentLevelIndex];
                        int mapSize = (int)currentLevel.layers[0].map.size();
                        float tileWidth, tileHeight;
                        updateTileSizes(currentLevel.zoomLevel, tileWidth, tileHeight);

                        SDL_FPoint cart = screenToCartesian((float)e.button.x, (float)e.button.y,
                            tileWidth, tileHeight,
                            currentLevel.mapOffsetX, currentLevel.mapOffsetY);

                        int gridX = (int)std::round(cart.x);
                        int gridY = (int)std::round(cart.y);

                        if (gridX >= 0 && gridX < mapSize && gridY >= 0 && gridY < mapSize) {
                            currentLevel.layers[currentLevel.activeLayer].map[gridX][gridY].tileID = gSelectedTileType;
                        }
                    }
                }
            }
            else if (e.button.button == SDL_BUTTON_RIGHT) {
                gIsDragging = true;
                gLastMouseX = (float)e.button.x;
                gLastMouseY = (float)e.button.y;
                gMouseDownX = (float)e.button.x;
                gMouseDownY = (float)e.button.y;
            }
        }
        else if (e.type == SDL_EVENT_MOUSE_BUTTON_UP) {
            if (e.button.button == SDL_BUTTON_RIGHT) {
                float dragDistance = std::sqrt(std::pow(e.button.x - gMouseDownX, 2) +
                    std::pow(e.button.y - gMouseDownY, 2));
                if (dragDistance < DRAG_THRESHOLD) {
                    // Right-click without drag - erase tile
                    if (!gLevels.empty()) {
                        Level& currentLevel = gLevels[gCurrentLevelIndex];
                        int mapSize = (int)currentLevel.layers[0].map.size();
                        float tileWidth, tileHeight;
                        updateTileSizes(currentLevel.zoomLevel, tileWidth, tileHeight);

                        SDL_FPoint cart = screenToCartesian((float)e.button.x, (float)e.button.y,
                            tileWidth, tileHeight,
                            currentLevel.mapOffsetX, currentLevel.mapOffsetY);

                        int gridX = (int)std::round(cart.x);
                        int gridY = (int)std::round(cart.y);

                        if (gridX >= 0 && gridX < mapSize && gridY >= 0 && gridY < mapSize) {
                            // Set to default for layer type
                            int defaultID = (currentLevel.activeLayer == 0) ? 1 : 0;
                            currentLevel.layers[currentLevel.activeLayer].map[gridX][gridY].tileID = defaultID;
                        }
                    }
                }
                gIsDragging = false;
            }
            else if (e.button.button == SDL_BUTTON_LEFT) {
                gIsDraggingScrollbar = false;
            }
        }
        else if (e.type == SDL_EVENT_MOUSE_MOTION) {
            if (gIsDragging && !gLevels.empty()) {
                Level& currentLevel = gLevels[gCurrentLevelIndex];
                float dx = e.motion.x - gLastMouseX;
                float dy = e.motion.y - gLastMouseY;
                currentLevel.mapOffsetX += dx;
                currentLevel.mapOffsetY += dy;
                currentLevel.targetOffsetX = currentLevel.mapOffsetX;
                currentLevel.targetOffsetY = currentLevel.mapOffsetY;
                gLastMouseX = (float)e.motion.x;
                gLastMouseY = (float)e.motion.y;
                gIsSnapping = false;
            }
            else if (gIsDraggingScrollbar) {
                int windowW, windowH;
                SDL_GetWindowSize(gWindow, &windowW, &windowH);
                float tileY = 50;
                float viewportHeight = windowH - tileY - 10;
                int totalTiles = (int)gTileTextures.size();
                float tileSize = 60;
                float spacing = 8;
                float contentHeight = totalTiles * (tileSize + spacing);
                float maxScroll = std::max(0.0f, contentHeight - viewportHeight);

                float thumbHeight = (viewportHeight / contentHeight) * viewportHeight;
                thumbHeight = std::max(20.0f, thumbHeight);

                float thumbY = e.motion.y - gScrollbarDragOffset;
                float scrollRatio = (thumbY - tileY) / (viewportHeight - thumbHeight);
                gTilePanelScroll = scrollRatio * maxScroll;
                gTilePanelScroll = std::max(0.0f, std::min(maxScroll, gTilePanelScroll));
            }
            else if (SDL_GetMouseState(nullptr, nullptr) & SDL_BUTTON_LMASK) {
                // Paint while dragging left mouse
                int windowW, windowH;
                SDL_GetWindowSize(gWindow, &windowW, &windowH);
                float panelX = (float)(windowW - TILE_PANEL_WIDTH);

                if (e.motion.x < panelX && !gLevels.empty()) {
                    Level& currentLevel = gLevels[gCurrentLevelIndex];
                    int mapSize = (int)currentLevel.layers[0].map.size();
                    float tileWidth, tileHeight;
                    updateTileSizes(currentLevel.zoomLevel, tileWidth, tileHeight);

                    SDL_FPoint cart = screenToCartesian((float)e.motion.x, (float)e.motion.y,
                        tileWidth, tileHeight,
                        currentLevel.mapOffsetX, currentLevel.mapOffsetY);

                    int gridX = (int)std::round(cart.x);
                    int gridY = (int)std::round(cart.y);

                    if (gridX >= 0 && gridX < mapSize && gridY >= 0 && gridY < mapSize) {
                        currentLevel.layers[currentLevel.activeLayer].map[gridX][gridY].tileID = gSelectedTileType;
                    }
                }
            }
        }
        else if (e.type == SDL_EVENT_MOUSE_WHEEL) {
            int windowW, windowH;
            SDL_GetWindowSize(gWindow, &windowW, &windowH);
            float panelX = (float)(windowW - TILE_PANEL_WIDTH);

            float mouseX, mouseY;
            SDL_GetMouseState(&mouseX, &mouseY);

            if (mouseX >= panelX) {
                // Scroll tile panel
                gTilePanelScroll -= e.wheel.y * 30.0f;
            }
            else if (!gLevels.empty()) {
                // Zoom map
                Level& currentLevel = gLevels[gCurrentLevelIndex];
                float oldZoom = currentLevel.zoomLevel;
                currentLevel.zoomLevel += e.wheel.y * 0.1f;
                currentLevel.zoomLevel = std::max(0.5f, std::min(3.0f, currentLevel.zoomLevel));

                if (oldZoom != currentLevel.zoomLevel) {
                    std::cout << "[Status] Zoom: " << (int)(currentLevel.zoomLevel * 100) << "%" << std::endl;
                }
            }
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