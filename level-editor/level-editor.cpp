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
#include <set>
#include <map>

// --- Configuration and Constants ---
const int INITIAL_SCREEN_WIDTH = 1280;
const int INITIAL_SCREEN_HEIGHT = 720;
const int TILE_PANEL_WIDTH = 250;
int gMapSize = 25;
const float BASE_TILE_WIDTH = 32.0f;
const int NUM_LAYERS = 6;

const std::string SAVE_PATH = "assets\\levels\\";
const std::string ASSET_PATH = "assets\\isometric tileset\\separated images\\";
const std::string COLLIDER_SAVE_FILE = SAVE_PATH + "tile_colliders.csv";

const std::string LAYER_NAMES[NUM_LAYERS] = { "Terrain", "Player", "Furniture", "Enemy", "NPCs", "Portal"};

// --- Data Structures ---

struct TileCollider {
    bool isSolid = false;
    int offsetLeft = 0;
    int offsetRight = 0;
    int offsetTop = 0;
    int offsetBottom = 0;
};

struct TileData {
    int tileID;
};

struct TileType {
    std::string name;
    int tileID;
    SDL_Texture* texture;
    TileCollider collider;
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
    Layer layers[NUM_LAYERS];
    int activeLayer = 0;
    float zoomLevel = 1.0f;
    float mapOffsetX = 0.0f;
    float mapOffsetY = 0.0f;
    float targetOffsetX = 0.0f;
    float targetOffsetY = 0.0f;
    std::string name = "";  // Custom level name (empty = use default)
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

bool gIsEditingLevelName = false;
std::string gLevelNameInput = "";

std::vector<SDL_Texture*> gTileTextures;

float gTilePanelScroll = 0.0f;
bool gIsDraggingScrollbar = false;
float gScrollbarDragOffset = 0.0f;

bool gShowColliderEditor = false;
float gColliderZoom = 5.0f;

// Forward declarations
bool init();
void close();
void handleEvents(bool& quit);
void updateCamera();
void draw();
bool loadLevels();
bool saveLevels();
void snapToNearestTile();
bool saveMetaCSV();
void drawColliderEditor();
void handleColliderInput(SDL_Event& e);
void saveTileColliders();
void loadTileColliders();

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

    int i = 0;
    int consecutiveMissing = 0;
    const int GAP_LIMIT = 20;

    while (consecutiveMissing < GAP_LIMIT) {
        std::string filename = "tile_" +
            std::string(3 - std::min(3, (int)std::to_string(i).length()), '0') +
            std::to_string(i) + ".png";
        std::string path = ASSET_PATH + filename;

        std::ifstream f(path.c_str());
        if (f.good()) {
            f.close();
            SDL_Texture* tex = loadTexture(path);
            gTileTextures.push_back(tex);
            consecutiveMissing = 0;

            bool found = false;
            for (auto& t : gTileTypes) {
                if (t.tileID == i) {
                    if (!t.texture) t.texture = tex;
                    found = true;
                    break;
                }
            }
            if (!found) {
                TileType newType;
                newType.name = "Tile " + std::to_string(i + 1);
                newType.tileID = i;
                newType.texture = tex;
                gTileTypes.push_back(newType);
            }
        }
        else {
            gTileTextures.push_back(nullptr);
            consecutiveMissing++;
        }
        i++;
    }

    while (!gTileTextures.empty() && gTileTextures.back() == nullptr) {
        gTileTextures.pop_back();
    }

    std::sort(gTileTypes.begin(), gTileTypes.end(), [](const TileType& a, const TileType& b) {
        return a.tileID < b.tileID;
        });

    std::cout << "[Status] Dynamically loaded " << gTileTextures.size() << " tile textures." << std::endl;
}

void freeTileTextures() {
    for (auto* tex : gTileTextures) {
        if (tex) SDL_DestroyTexture(tex);
    }
    gTileTextures.clear();
    gTileTypes.clear();
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

    if (layerIndex == 0) {
        layer.map = generateMap(mapSize, 1);
    }
    else if (layerIndex == 1) {
        layer.map = generateMap(mapSize, 0);
        layer.map[0][0].tileID = 3;
    }
    else {
        layer.map = generateMap(mapSize, 0);
    }
    return layer;
}

// Helper function to get level file prefix
std::string getLevelPrefix(const Level& level, size_t index) {
    if (!level.name.empty()) {
        return level.name;
    }
    return "level_" + std::to_string(index + 1);
}

// --- Meta CSV Export ---
bool saveMetaCSV() {
    std::cout << "[Action] Generating metadata for all levels..." << std::endl;

    for (size_t levelIdx = 0; levelIdx < gLevels.size(); ++levelIdx) {
        const auto& level = gLevels[levelIdx];
        int mapSize = (int)level.layers[0].map.size();
        std::map<int, std::set<int>> tileUsage;

        for (int layerIdx = 0; layerIdx < NUM_LAYERS; ++layerIdx) {
            const auto& layer = level.layers[layerIdx];
            int layerMapSize = (int)layer.map.size();

            for (int x = 0; x < layerMapSize; ++x) {
                for (int y = 0; y < layerMapSize; ++y) {
                    int tileID = layer.map[x][y].tileID;
                    if (layerIdx > 0 && tileID == 0) continue;
                    tileUsage[tileID].insert(layerIdx);
                }
            }
        }

        std::string prefix = getLevelPrefix(level, levelIdx);
        std::string metaFilename = SAVE_PATH + prefix + "_metadata.csv";
        std::ofstream metaFile(metaFilename);

        if (!metaFile.is_open()) {
            std::cerr << "Error: Could not create " << metaFilename << std::endl;
            continue;
        }

        metaFile << "Asset_name,asset_id,layer_name,layer_number,grid_height,grid_width\n";

        for (const auto& entry : tileUsage) {
            int tileID = entry.first;
            const std::set<int>& layers = entry.second;

            for (int layerIdx : layers) {
                std::string assetName = "tile_" + std::to_string(tileID);
                metaFile << assetName << "," << tileID << "," << LAYER_NAMES[layerIdx] << ","
                    << layerIdx << "," << mapSize << "," << mapSize << "\n";
            }
        }
        metaFile.close();
        std::cout << "[Status] Created " << metaFilename << std::endl;
    }
    return true;
}

// --- Collider Save/Load ---
void saveTileColliders() {
    std::cout << "[Action] Saving tile colliders..." << std::endl;
    std::ofstream file(COLLIDER_SAVE_FILE);

    if (!file.is_open()) {
        std::cerr << "Error: Could not open collider file for writing." << std::endl;
        return;
    }

    file << "tileID,isSolid,offsetLeft,offsetRight,offsetTop,offsetBottom\n";

    int count = 0;
    for (const auto& tile : gTileTypes) {
        file << (tile.tileID + 1) << ","
            << (tile.collider.isSolid ? "1" : "0") << ","
            << tile.collider.offsetLeft << ","
            << tile.collider.offsetRight << ","
            << tile.collider.offsetTop << ","
            << tile.collider.offsetBottom << "\n";
        count++;
    }

    file.flush();
    file.close();
    std::cout << "[Status] Saved collider data for " << count << " tiles." << std::endl;
}

void loadTileColliders() {
    std::cout << "[Action] Loading tile colliders..." << std::endl;
    std::ifstream file(COLLIDER_SAVE_FILE);

    if (!file.is_open()) {
        std::cout << "[Info] No collider file found. Using defaults." << std::endl;
        return;
    }

    std::string line;
    std::getline(file, line);

    int loadedCount = 0;

    while (std::getline(file, line)) {
        if (line.empty()) continue;

        for (char& c : line) {
            if (c == ',') c = ' ';
        }

        std::stringstream ss(line);
        int rawID, isSolid, offL, offR, offT, offB;

        if (ss >> rawID >> isSolid >> offL >> offR >> offT >> offB) {
            int internalID = rawID - 1;

            for (auto& tile : gTileTypes) {
                if (tile.tileID == internalID) {
                    tile.collider.isSolid = (isSolid == 1);
                    tile.collider.offsetLeft = offL;
                    tile.collider.offsetRight = offR;
                    tile.collider.offsetTop = offT;
                    tile.collider.offsetBottom = offB;
                    loadedCount++;
                    break;
                }
            }
        }
    }

    file.close();
    std::cout << "[Status] Loaded collider data for " << loadedCount << " tiles." << std::endl;
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
            level.name = "";
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
    level.name = "";
    gLevels.push_back(level);
    gCurrentLevelIndex = gLevels.size() - 1;
    std::cout << "[Status] New level created." << std::endl;
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

// --- Serialization ---
bool saveLevels() {
    std::cout << "[Action] Saving all levels..." << std::endl;
    saveTileColliders();

    for (size_t i = 0; i < gLevels.size(); ++i) {
        const auto& level = gLevels[i];
        int mapSize = (int)level.layers[0].map.size();
        std::string prefix = getLevelPrefix(level, i);

        // Save meta file with level name
        std::string metaFilename = SAVE_PATH + prefix + "_meta.csv";
        std::ofstream metaFile(metaFilename);
        if (metaFile.is_open()) {
            metaFile << "zoomLevel,mapOffsetX,mapOffsetY,mapSize,activeLayer,levelName\n";
            metaFile << level.zoomLevel << "," << level.mapOffsetX << "," << level.mapOffsetY << ","
                << mapSize << "," << level.activeLayer << "," << level.name << "\n";
            metaFile.close();
        }

        const char* layerSuffix[NUM_LAYERS] = { "terrain", "player", "furniture", "enemy", "npcs", "portal"};

        for (int layerIdx = 0; layerIdx < NUM_LAYERS; ++layerIdx) {
            std::string filename = SAVE_PATH + prefix + "_" + layerSuffix[layerIdx] + ".csv";
            std::ofstream file(filename);

            if (!file.is_open()) {
                std::cerr << "Error: Could not save " << filename << std::endl;
                continue;
            }

            const auto& layer = level.layers[layerIdx];

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

    saveMetaCSV();
    std::cout << "[Status] Save successful." << std::endl;
    return true;
}

bool loadLevels() {
    std::cout << "[Action] Searching for level files..." << std::endl;
    gLevels.clear();
    int levelIndex = 1;

    while (true) {
        // Try default naming first
        std::string metaFilename = SAVE_PATH + "level_" + std::to_string(levelIndex) + "_metadata.csv";
        std::ifstream metaFile(metaFilename);

        if (!metaFile.is_open()) break;

        try {
            Level level;
            std::string line, value;

            // Try to load the _meta.csv for camera/name info
            std::string levelMetaFile = SAVE_PATH + "level_" + std::to_string(levelIndex) + "_meta.csv";
            std::ifstream lmf(levelMetaFile);
            if (lmf.is_open()) {
                std::getline(lmf, line); // Skip header
                if (std::getline(lmf, line)) {
                    std::stringstream ss(line);
                    if (std::getline(ss, value, ',')) level.zoomLevel = std::stof(value);
                    if (std::getline(ss, value, ',')) level.mapOffsetX = std::stof(value);
                    if (std::getline(ss, value, ',')) level.mapOffsetY = std::stof(value);
                    int mapSize = gMapSize;
                    if (std::getline(ss, value, ',')) mapSize = std::stoi(value);
                    if (std::getline(ss, value, ',')) level.activeLayer = std::stoi(value);
                    if (std::getline(ss, value, ',')) level.name = value;
                    gMapSize = mapSize;
                }
                lmf.close();
            }

            level.targetOffsetX = level.mapOffsetX;
            level.targetOffsetY = level.mapOffsetY;

            // Now read tile usage from metadata to get mapSize
            if (std::getline(metaFile, line)) {
                // Skip header, read first data line for grid size
                if (std::getline(metaFile, line)) {
                    std::stringstream ss(line);
                    std::string temp;
                    for (int skip = 0; skip < 4; skip++) std::getline(ss, temp, ',');
                    if (std::getline(ss, temp, ',')) {
                        int mapSize = std::stoi(temp);
                        gMapSize = mapSize;
                    }
                }
            }
            metaFile.close();

            const char* layerSuffix[NUM_LAYERS] = { "terrain", "player", "furniture", "enemy", "npcs", "portal" };

            for (int layerIdx = 0; layerIdx < NUM_LAYERS; ++layerIdx) {
                std::string layerFilename = SAVE_PATH + "level_" + std::to_string(levelIndex) + "_" +
                    layerSuffix[layerIdx] + ".csv";
                std::ifstream layerFile(layerFilename);

                Layer layer;
                layer.name = LAYER_NAMES[layerIdx];
                layer.visible = true;
                layer.map = generateMap(gMapSize, 0);

                if (layerFile.is_open()) {
                    for (int y = 0; y < gMapSize; ++y) {
                        if (!std::getline(layerFile, line)) break;
                        std::stringstream rowStream(line);
                        for (int x = 0; x < gMapSize; ++x) {
                            if (!std::getline(rowStream, value, ',')) break;
                            layer.map[x][y].tileID = std::stoi(value);
                        }
                    }
                    layerFile.close();
                }

                level.layers[layerIdx] = layer;
            }

            gLevels.push_back(level);
        }
        catch (const std::exception& e) {
            std::cerr << "Error loading level " << levelIndex << ": " << e.what() << std::endl;
        }

        levelIndex++;
    }

    if (!gLevels.empty()) {
        gCurrentLevelIndex = 0;
        gGridSizeInput = std::to_string(gMapSize);
        std::cout << "[Status] " << gLevels.size() << " level(s) loaded." << std::endl;
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
        std::cerr << "SDL_ttf could not initialize!" << std::endl;
        return false;
    }

    gWindow = SDL_CreateWindow("Osiris Level Editor (SDL3)", INITIAL_SCREEN_WIDTH, INITIAL_SCREEN_HEIGHT, SDL_WINDOW_RESIZABLE);
    if (!gWindow) {
        std::cerr << "Window creation failed: " << SDL_GetError() << std::endl;
        return false;
    }

    gRenderer = SDL_CreateRenderer(gWindow, nullptr);
    if (!gRenderer) {
        std::cerr << "Renderer creation failed: " << SDL_GetError() << std::endl;
        return false;
    }

    gFont = TTF_OpenFont("assets/font/font.ttf", 16);
    if (!gFont) {
        std::cerr << "Failed to load font!" << std::endl;
    }

    SDL_SetRenderDrawBlendMode(gRenderer, SDL_BLENDMODE_BLEND);
    loadTileTextures();
    loadTileColliders();

    return true;
}

void close() {
    saveLevels();
    freeTileTextures();
    if (gFont) TTF_CloseFont(gFont);
    SDL_DestroyRenderer(gRenderer);
    SDL_DestroyWindow(gWindow);
    TTF_Quit();
    SDL_Quit();
}

// --- Rendering Functions ---
void drawColliderEditor() {
    if (!gShowColliderEditor) return;

    int windowW, windowH;
    SDL_GetWindowSize(gWindow, &windowW, &windowH);

    SDL_SetRenderDrawBlendMode(gRenderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(gRenderer, 0, 0, 0, 200);
    SDL_FRect fullscreen = { 0, 0, (float)windowW, (float)windowH };
    SDL_RenderFillRect(gRenderer, &fullscreen);

    if (gSelectedTileType < 0 || gSelectedTileType >= (int)gTileTypes.size()) return;
    TileType& tile = gTileTypes[gSelectedTileType];

    renderText("COLLIDER EDITOR - Tile " + std::to_string(tile.tileID), (float)windowW / 2 - 100, 50, { 255, 255, 255, 255 });
    renderText("Press ESC to Close", (float)windowW / 2 - 60, 80, { 180, 180, 180, 255 });

    SDL_FRect solidBtn = { (float)windowW / 2 - 100, (float)windowH / 2 - 250, 200, 30 };
    SDL_SetRenderDrawColor(gRenderer, 60, 60, 60, 255);
    SDL_RenderFillRect(gRenderer, &solidBtn);
    SDL_SetRenderDrawColor(gRenderer, 255, 255, 255, 255);
    SDL_RenderRect(gRenderer, &solidBtn);

    std::string btnText = std::string("Solid: ") + (tile.collider.isSolid ? "YES" : "NO");
    SDL_Color btnColor = tile.collider.isSolid ? SDL_Color{ 100, 255, 100, 255 } : SDL_Color{ 255, 100, 100, 255 };
    renderText(btnText, solidBtn.x + 60, solidBtn.y + 5, btnColor);

    if (!tile.texture) return;

    float texW, texH;
    SDL_GetTextureSize(tile.texture, &texW, &texH);

    float previewW = texW * gColliderZoom;
    float previewH = texH * gColliderZoom;
    float previewX = (windowW - previewW) / 2;
    float previewY = (windowH - previewH) / 2;

    SDL_SetRenderDrawColor(gRenderer, 30, 30, 30, 255);
    SDL_FRect bgRect2 = { previewX, previewY, previewW, previewH };
    SDL_RenderFillRect(gRenderer, &bgRect2);

    SDL_FRect dstRect = { previewX, previewY, previewW, previewH };
    SDL_RenderTexture(gRenderer, tile.texture, nullptr, &dstRect);
    SDL_SetRenderDrawColor(gRenderer, 255, 255, 255, 255);
    SDL_RenderRect(gRenderer, &dstRect);

    SDL_SetRenderDrawColor(gRenderer, 100, 100, 100, 80);
    for (int i = 0; i <= (int)texW; i++) {
        float x = previewX + i * gColliderZoom;
        SDL_RenderLine(gRenderer, x, previewY, x, previewY + previewH);
    }
    for (int i = 0; i <= (int)texH; i++) {
        float y = previewY + i * gColliderZoom;
        SDL_RenderLine(gRenderer, previewX, y, previewX + previewW, y);
    }

    if (tile.collider.isSolid) {
        float boxX = previewX + (tile.collider.offsetLeft * gColliderZoom);
        float boxY = previewY + (tile.collider.offsetTop * gColliderZoom);
        float boxW = previewW - ((tile.collider.offsetLeft + tile.collider.offsetRight) * gColliderZoom);
        float boxH = previewH - ((tile.collider.offsetTop + tile.collider.offsetBottom) * gColliderZoom);

        SDL_SetRenderDrawColor(gRenderer, 255, 0, 0, 80);
        SDL_FRect colRect = { boxX, boxY, boxW, boxH };
        SDL_RenderFillRect(gRenderer, &colRect);
        SDL_SetRenderDrawColor(gRenderer, 255, 0, 0, 255);
        SDL_RenderRect(gRenderer, &colRect);

        renderText("T: " + std::to_string(tile.collider.offsetTop), (float)windowW / 2, previewY - 25, { 255, 255, 0, 255 });
        renderText("B: " + std::to_string(tile.collider.offsetBottom), (float)windowW / 2, previewY + previewH + 10, { 255, 255, 0, 255 });
        renderText("L: " + std::to_string(tile.collider.offsetLeft), previewX - 50, (float)windowH / 2, { 255, 255, 0, 255 });
        renderText("R: " + std::to_string(tile.collider.offsetRight), previewX + previewW + 10, (float)windowH / 2, { 255, 255, 0, 255 });
    }
}

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
            renderText("[C] Edit Collider", itemX + 65, itemY + 42, { 255, 255, 0, 255 });
        }

        if (i < (int)gTileTextures.size() && gTileTextures[i]) {
            float previewSize = 50;
            float centerX = itemX + 25;
            float centerY = itemY + tileSize / 2;

            SDL_FRect destRect = { centerX - previewSize / 2, centerY - previewSize / 2, previewSize, previewSize };
            SDL_RenderTexture(gRenderer, gTileTextures[i], nullptr, &destRect);
        }

        SDL_Color textColor = { 220, 220, 220, 255 };
        renderText("Tile " + std::to_string(i), itemX + 65, itemY + 22, textColor);
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
    if (px + tileWidth < 0 || px - tileWidth > windowW || py + tileHeight < 0 || py - tileHeight * 3 > windowH) return;

    SDL_Texture* tileTexture = nullptr;
    if (data.tileID >= 0 && data.tileID < (int)gTileTextures.size()) {
        tileTexture = gTileTextures[data.tileID];
    }

    if (tileTexture) {
        SDL_SetTextureAlphaMod(tileTexture, alpha);
        float texW, texH;
        SDL_GetTextureSize(tileTexture, &texW, &texH);

        float scale = tileWidth / BASE_TILE_WIDTH;
        float scaledWidth = texW * scale;
        float scaledHeight = texH * scale;

        SDL_FRect destRect = { px - scaledWidth / 2.0f, py - scaledHeight / 2.0f, scaledWidth, scaledHeight };
        SDL_RenderTexture(gRenderer, tileTexture, nullptr, &destRect);
        SDL_SetTextureAlphaMod(tileTexture, 255);
    }
}

void drawMap() {
    if (gLevels.empty()) return;
    Level& currentLevel = gLevels[gCurrentLevelIndex];

    if (currentLevel.layers[0].map.empty()) return;
    int mapSize = (int)currentLevel.layers[0].map.size();
    float tileWidth, tileHeight;
    updateTileSizes(currentLevel.zoomLevel, tileWidth, tileHeight);

    for (int layerIdx = 0; layerIdx < NUM_LAYERS; ++layerIdx) {
        const Layer& layer = currentLevel.layers[layerIdx];
        if (!layer.visible || layer.map.empty()) continue;

        int alpha = (layerIdx == currentLevel.activeLayer) ? 255 : 128;

        for (int y = 0; y < mapSize; ++y) {
            for (int x = 0; x < mapSize; ++x) {
                if (x >= (int)layer.map.size() || y >= (int)layer.map[x].size()) continue;
                const TileData& data = layer.map[x][y];
                if (layerIdx > 0 && data.tileID == 0) continue;
                drawTile(x, y, data, tileWidth, tileHeight, currentLevel.mapOffsetX, currentLevel.mapOffsetY, alpha);
            }
        }
    }
}

void drawOverlay() {
    int windowW, windowH;
    SDL_GetWindowSize(gWindow, &windowW, &windowH);
    SDL_Color textColor = { 220, 220, 220, 255 };

    // === Level Name Input (Top Left) ===
    SDL_SetRenderDrawColor(gRenderer, 20, 20, 20, 180);
    SDL_FRect nameBgRect = { 10, 10, 260, 35 };
    SDL_RenderFillRect(gRenderer, &nameBgRect);
    renderText("Level Name:", 20, 18, textColor);

    float nameLabelW = 95;
    SDL_FRect nameValueRect = { 20 + nameLabelW + 5, 15, 145, 25 };

    if (gIsEditingLevelName) {
        SDL_SetRenderDrawColor(gRenderer, 100, 150, 255, 255);
    }
    else {
        SDL_SetRenderDrawColor(gRenderer, 60, 60, 60, 255);
    }
    SDL_RenderFillRect(gRenderer, &nameValueRect);
    SDL_SetRenderDrawColor(gRenderer, 240, 240, 240, 255);
    SDL_RenderRect(gRenderer, &nameValueRect);

    std::string displayName;
    if (gIsEditingLevelName) {
        displayName = gLevelNameInput;
    }
    else if (!gLevels.empty() && !gLevels[gCurrentLevelIndex].name.empty()) {
        displayName = gLevels[gCurrentLevelIndex].name;
    }
    else {
        displayName = "(default)";
    }
    renderText(displayName, nameValueRect.x + 5, nameValueRect.y + 5, textColor);

    if (gIsEditingLevelName && SDL_GetTicks() % 1000 < 500) {
        int cursorW = 0, cursorH = 0;
        if (gFont && !gLevelNameInput.empty()) {
            TTF_GetStringSizeWrapped(gFont, gLevelNameInput.c_str(), 0, 0, &cursorW, &cursorH);
        }
        else {
            cursorW = (int)gLevelNameInput.length() * 8;
        }
        float cursorX = nameValueRect.x + 5 + (float)cursorW;
        SDL_SetRenderDrawColor(gRenderer, 255, 255, 255, 255);
        SDL_RenderLine(gRenderer, cursorX, nameValueRect.y + 5, cursorX, nameValueRect.y + 20);
    }

    // === Level/Layer Info (Bottom Left) ===
    if (!gLevels.empty()) {
        Level& currentLevel = gLevels[gCurrentLevelIndex];

        std::string levelText = "Level: " + std::to_string(gCurrentLevelIndex + 1) + " / " + std::to_string(gLevels.size());
        int textW = 0, textH = 0;
        if (gFont) TTF_GetStringSizeWrapped(gFont, levelText.c_str(), 0, 0, &textW, &textH);
        else textW = (int)levelText.length() * 8;

        SDL_SetRenderDrawColor(gRenderer, 20, 20, 20, 180);
        SDL_FRect bgRect = { 10, (float)windowH - 35, (float)textW + 10.0f, 25 };
        SDL_RenderFillRect(gRenderer, &bgRect);
        renderText(levelText, 15, (float)windowH - 32, textColor);

        std::string layerText = "Layer: " + currentLevel.layers[currentLevel.activeLayer].name +
            " (" + std::to_string(currentLevel.activeLayer + 1) + "/" + std::to_string(NUM_LAYERS) + ") | Visible: ";
        for (int i = 0; i < NUM_LAYERS; ++i) {
            if (currentLevel.layers[i].visible) layerText += LAYER_NAMES[i][0];
        }

        int layerTextW = 0;
        if (gFont) TTF_GetStringSizeWrapped(gFont, layerText.c_str(), 0, 0, &layerTextW, &textH);
        else layerTextW = (int)layerText.length() * 8;

        SDL_SetRenderDrawColor(gRenderer, 20, 20, 20, 180);
        SDL_FRect layerBgRect = { 10, (float)windowH - 65, (float)layerTextW + 10.0f, 25 };
        SDL_RenderFillRect(gRenderer, &layerBgRect);
        renderText(layerText, 15, (float)windowH - 62, { 100, 200, 255, 255 });
    }

    // === Grid Size Input (Top Right) ===
    float uiWidth = 200;
    float uiX = (float)windowW - uiWidth - 10 - TILE_PANEL_WIDTH;
    SDL_SetRenderDrawColor(gRenderer, 20, 20, 20, 180);
    SDL_FRect bgRect2 = { uiX, 10, uiWidth, 35 };
    SDL_RenderFillRect(gRenderer, &bgRect2);
    renderText("Grid Size:", uiX + 10, 18, textColor);

    int labelW = 80;
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
        if (gFont && !cursorText.empty()) TTF_GetStringSizeWrapped(gFont, cursorText.c_str(), 0, 0, &cursorW, &cursorH);
        else cursorW = (int)cursorText.length() * 8;
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
    drawColliderEditor();
    SDL_RenderPresent(gRenderer);
}

// --- Input Handling ---
void handleColliderInput(SDL_Event& e) {
    if (!gShowColliderEditor) return;
    if (gSelectedTileType < 0 || gSelectedTileType >= (int)gTileTypes.size()) return;
    TileType& tile = gTileTypes[gSelectedTileType];

    if (e.type == SDL_EVENT_KEY_DOWN) {
        if (e.key.key == SDLK_ESCAPE) gShowColliderEditor = false;
    }
    else if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_LEFT) {
        int windowW, windowH;
        SDL_GetWindowSize(gWindow, &windowW, &windowH);

        SDL_FRect solidToggle = { (float)windowW / 2 - 100, (float)windowH / 2 - 250, 200, 30 };
        if (e.button.x >= solidToggle.x && e.button.x <= solidToggle.x + solidToggle.w &&
            e.button.y >= solidToggle.y && e.button.y <= solidToggle.y + solidToggle.h) {
            tile.collider.isSolid = !tile.collider.isSolid;
            return;
        }

        if (!tile.collider.isSolid || !tile.texture) return;

        float texW, texH;
        SDL_GetTextureSize(tile.texture, &texW, &texH);
        float previewW = texW * gColliderZoom;
        float previewH = texH * gColliderZoom;
        float previewX = (windowW - previewW) / 2;
        float previewY = (windowH - previewH) / 2;

        if (e.button.x >= previewX && e.button.x <= previewX + previewW &&
            e.button.y >= previewY && e.button.y <= previewY + previewH) {

            float localX = (e.button.x - previewX) / gColliderZoom;
            float localY = (e.button.y - previewY) / gColliderZoom;

            float distToLeft = localX, distToRight = texW - localX;
            float distToTop = localY, distToBottom = texH - localY;
            float minH = std::min(distToLeft, distToRight);
            float minV = std::min(distToTop, distToBottom);

            if (minH < minV) {
                if (distToLeft < distToRight) tile.collider.offsetLeft = (int)localX;
                else tile.collider.offsetRight = (int)(texW - localX);
            }
            else {
                if (distToTop < distToBottom) tile.collider.offsetTop = (int)localY;
                else tile.collider.offsetBottom = (int)(texH - localY);
            }

            if (tile.collider.offsetLeft + tile.collider.offsetRight >= texW)
                tile.collider.offsetRight = (int)texW - tile.collider.offsetLeft - 1;
            if (tile.collider.offsetTop + tile.collider.offsetBottom >= texH)
                tile.collider.offsetBottom = (int)texH - tile.collider.offsetTop - 1;
        }
    }
}

void handleEvents(bool& quit) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_EVENT_QUIT) { quit = true; return; }

        if (gShowColliderEditor) { handleColliderInput(e); continue; }

        if (e.type == SDL_EVENT_KEY_DOWN) {
            // Handle Level Name Input
            if (gIsEditingLevelName) {
                if (e.key.key == SDLK_RETURN || e.key.key == SDLK_KP_ENTER) {
                    if (!gLevels.empty()) {
                        gLevels[gCurrentLevelIndex].name = gLevelNameInput;
                        std::cout << "[Status] Level name set to: " << (gLevelNameInput.empty() ? "(default)" : gLevelNameInput) << std::endl;
                    }
                    gIsEditingLevelName = false;
                    SDL_StopTextInput(gWindow);
                }
                else if (e.key.key == SDLK_ESCAPE) {
                    if (!gLevels.empty()) gLevelNameInput = gLevels[gCurrentLevelIndex].name;
                    gIsEditingLevelName = false;
                    SDL_StopTextInput(gWindow);
                }
                else if (e.key.key == SDLK_BACKSPACE && !gLevelNameInput.empty()) {
                    gLevelNameInput.pop_back();
                }
            }
            // Handle Grid Size Input
            else if (gIsEditingGridSize) {
                if (e.key.key == SDLK_RETURN || e.key.key == SDLK_KP_ENTER) {
                    if (!gGridSizeInput.empty()) {
                        try {
                            int newSize = std::stoi(gGridSizeInput);
                            if (newSize >= 5 && newSize <= 100) {
                                gMapSize = newSize;
                                std::cout << "[Status] Grid size changed to " << gMapSize << "x" << gMapSize << std::endl;
                                for (auto& level : gLevels) {
                                    for (int i = 0; i < NUM_LAYERS; ++i) {
                                        level.layers[i] = createLayer(gMapSize, LAYER_NAMES[i], i);
                                    }
                                }
                            }
                        }
                        catch (...) {}
                    }
                    gIsEditingGridSize = false;
                    SDL_StopTextInput(gWindow);
                }
                else if (e.key.key == SDLK_ESCAPE) {
                    gGridSizeInput = std::to_string(gMapSize);
                    gIsEditingGridSize = false;
                    SDL_StopTextInput(gWindow);
                }
                else if (e.key.key == SDLK_BACKSPACE && !gGridSizeInput.empty()) {
                    gGridSizeInput.pop_back();
                }
            }
            // Normal keyboard shortcuts
            else {
                if (e.key.key == SDLK_S && (e.key.mod & SDL_KMOD_CTRL)) saveLevels();
                else if (e.key.key == SDLK_N && (e.key.mod & SDL_KMOD_CTRL)) addNewLevel();
                else if (e.key.key == SDLK_D && (e.key.mod & SDL_KMOD_CTRL)) deleteCurrentLevel();
                else if (e.key.key == SDLK_LEFT && gCurrentLevelIndex > 0) {
                    gCurrentLevelIndex--;
                    std::cout << "[Status] Switched to Level " << gCurrentLevelIndex + 1 << std::endl;
                }
                else if (e.key.key == SDLK_RIGHT && gCurrentLevelIndex < gLevels.size() - 1) {
                    gCurrentLevelIndex++;
                    std::cout << "[Status] Switched to Level " << gCurrentLevelIndex + 1 << std::endl;
                }
                else if (e.key.key == SDLK_TAB && !gLevels.empty()) {
                    Level& cl = gLevels[gCurrentLevelIndex];
                    cl.activeLayer = (cl.activeLayer + 1) % NUM_LAYERS;
                }
                else if (e.key.key == SDLK_V && !gLevels.empty()) {
                    Level& cl = gLevels[gCurrentLevelIndex];
                    cl.layers[cl.activeLayer].visible = !cl.layers[cl.activeLayer].visible;
                }
                else if (e.key.key >= SDLK_1 && e.key.key <= SDLK_5 && !gLevels.empty()) {
                    int layerNum = e.key.key - SDLK_1;
                    if (layerNum < NUM_LAYERS) gLevels[gCurrentLevelIndex].activeLayer = layerNum;
                }
                else if (e.key.key == SDLK_SPACE) snapToNearestTile();
                else if (e.key.key == SDLK_C) gShowColliderEditor = true;
            }
        }
        else if (e.type == SDL_EVENT_TEXT_INPUT) {
            if (gIsEditingLevelName) {
                for (int i = 0; e.text.text[i] != '\0'; ++i) {
                    char c = e.text.text[i];
                    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                        (c >= '0' && c <= '9') || c == '_') {
                        if (gLevelNameInput.length() < 20) gLevelNameInput += c;
                    }
                }
            }
            else if (gIsEditingGridSize) {
                for (int i = 0; e.text.text[i] != '\0'; ++i) {
                    if (e.text.text[i] >= '0' && e.text.text[i] <= '9') {
                        if (gGridSizeInput.length() < 3) gGridSizeInput += e.text.text[i];
                    }
                }
            }
        }
        else if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
            if (e.button.button == SDL_BUTTON_LEFT) {
                int windowW, windowH;
                SDL_GetWindowSize(gWindow, &windowW, &windowH);
                float panelX = (float)(windowW - TILE_PANEL_WIDTH);

                // Check Level Name Input click
                float nameLabelW = 95;
                SDL_FRect nameValueRect = { 20 + nameLabelW + 5, 15, 145, 25 };
                if (e.button.x >= nameValueRect.x && e.button.x <= nameValueRect.x + nameValueRect.w &&
                    e.button.y >= nameValueRect.y && e.button.y <= nameValueRect.y + nameValueRect.h) {
                    gIsEditingLevelName = true;
                    gIsEditingGridSize = false;
                    if (!gLevels.empty()) gLevelNameInput = gLevels[gCurrentLevelIndex].name;
                    SDL_StartTextInput(gWindow);
                    continue;
                }

                // Check Grid Size Input click
                float uiWidth = 200;
                float uiX = (float)windowW - uiWidth - 10 - TILE_PANEL_WIDTH;
                int labelW = 80;
                SDL_FRect valueRect = { uiX + 10 + (float)labelW + 5, 15, 60, 25 };
                if (e.button.x >= valueRect.x && e.button.x <= valueRect.x + valueRect.w &&
                    e.button.y >= valueRect.y && e.button.y <= valueRect.y + valueRect.h) {
                    gIsEditingGridSize = true;
                    gIsEditingLevelName = false;
                    gGridSizeInput = "";
                    SDL_StartTextInput(gWindow);
                    continue;
                }

                // Tile Panel
                if (e.button.x >= panelX) {
                    float tileY = 50, tileSize = 60, spacing = 8;
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
                            float thumbHeight = std::max(20.0f, (viewportHeight / contentHeight) * viewportHeight);
                            float thumbY = tileY + (gTilePanelScroll / maxScroll) * (viewportHeight - thumbHeight);
                            gScrollbarDragOffset = e.button.y - thumbY;
                        }
                    }

                    if (!gIsDraggingScrollbar && e.button.y >= tileY) {
                        for (int i = 0; i < totalTiles; ++i) {
                            float itemY = tileY + (i * (tileSize + spacing)) - gTilePanelScroll;
                            if (e.button.y >= itemY && e.button.y <= itemY + tileSize) {
                                gSelectedTileType = i;
                                break;
                            }
                        }
                    }
                }
                else {
                    // Map area click
                    gIsEditingGridSize = false;
                    gIsEditingLevelName = false;
                    SDL_StopTextInput(gWindow);
                    gMouseDownX = (float)e.button.x;
                    gMouseDownY = (float)e.button.y;

                    if (!gLevels.empty()) {
                        Level& currentLevel = gLevels[gCurrentLevelIndex];
                        if (!currentLevel.layers[currentLevel.activeLayer].map.empty()) {
                            int mapSize = (int)currentLevel.layers[0].map.size();
                            float tileWidth, tileHeight;
                            updateTileSizes(currentLevel.zoomLevel, tileWidth, tileHeight);

                            SDL_FPoint cart = screenToCartesian((float)e.button.x, (float)e.button.y,
                                tileWidth, tileHeight, currentLevel.mapOffsetX, currentLevel.mapOffsetY);

                            int gridX = (int)std::round(cart.x);
                            int gridY = (int)std::round(cart.y);

                            if (gridX >= 0 && gridX < mapSize && gridY >= 0 && gridY < mapSize) {
                                currentLevel.layers[currentLevel.activeLayer].map[gridX][gridY].tileID = gSelectedTileType;
                            }
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
                float dragDistance = std::sqrt(std::pow(e.button.x - gMouseDownX, 2) + std::pow(e.button.y - gMouseDownY, 2));
                if (dragDistance < DRAG_THRESHOLD && !gLevels.empty()) {
                    Level& currentLevel = gLevels[gCurrentLevelIndex];
                    if (!currentLevel.layers[currentLevel.activeLayer].map.empty()) {
                        int mapSize = (int)currentLevel.layers[0].map.size();
                        float tileWidth, tileHeight;
                        updateTileSizes(currentLevel.zoomLevel, tileWidth, tileHeight);

                        SDL_FPoint cart = screenToCartesian((float)e.button.x, (float)e.button.y,
                            tileWidth, tileHeight, currentLevel.mapOffsetX, currentLevel.mapOffsetY);

                        int gridX = (int)std::round(cart.x);
                        int gridY = (int)std::round(cart.y);

                        if (gridX >= 0 && gridX < mapSize && gridY >= 0 && gridY < mapSize) {
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
                float tileSize = 60, spacing = 8;
                float contentHeight = totalTiles * (tileSize + spacing);
                float maxScroll = std::max(0.0f, contentHeight - viewportHeight);
                float thumbHeight = std::max(20.0f, (viewportHeight / contentHeight) * viewportHeight);
                float thumbY = e.motion.y - gScrollbarDragOffset;
                float scrollRatio = (thumbY - tileY) / (viewportHeight - thumbHeight);
                gTilePanelScroll = std::max(0.0f, std::min(maxScroll, scrollRatio * maxScroll));
            }
            else if (SDL_GetMouseState(nullptr, nullptr) & SDL_BUTTON_LMASK) {
                int windowW, windowH;
                SDL_GetWindowSize(gWindow, &windowW, &windowH);
                float panelX = (float)(windowW - TILE_PANEL_WIDTH);

                if (e.motion.x < panelX && !gLevels.empty()) {
                    Level& currentLevel = gLevels[gCurrentLevelIndex];
                    if (!currentLevel.layers[currentLevel.activeLayer].map.empty()) {
                        int mapSize = (int)currentLevel.layers[0].map.size();
                        float tileWidth, tileHeight;
                        updateTileSizes(currentLevel.zoomLevel, tileWidth, tileHeight);

                        SDL_FPoint cart = screenToCartesian((float)e.motion.x, (float)e.motion.y,
                            tileWidth, tileHeight, currentLevel.mapOffsetX, currentLevel.mapOffsetY);

                        int gridX = (int)std::round(cart.x);
                        int gridY = (int)std::round(cart.y);

                        if (gridX >= 0 && gridX < mapSize && gridY >= 0 && gridY < mapSize) {
                            currentLevel.layers[currentLevel.activeLayer].map[gridX][gridY].tileID = gSelectedTileType;
                        }
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
                gTilePanelScroll -= e.wheel.y * 30.0f;
            }
            else if (!gLevels.empty()) {
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