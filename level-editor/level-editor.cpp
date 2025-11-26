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
#include <iomanip> 

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

const std::string LAYER_NAMES[NUM_LAYERS] = { "Terrain", "Player", "Furniture", "Enemy", "NPCs", "Portal" };

// --- Data Structures ---

struct TileCollider {
    bool isSolid = false;
    int offsetLeft = 0;
    int offsetRight = 0;
    int offsetTop = 0;
    int offsetBottom = 0;
};

// Updated TileData to include scale
struct TileData {
    int tileID;
    float scale = 1.0f;
};

struct TileType {
    std::string name;
    int tileID;
    SDL_Texture* texture;
    TileCollider collider;
};

std::vector<TileType> gTileTypes;
int gSelectedTileType = 0;
float gCurrentScale = 1.0f; // Global brush scale

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
    std::string name = "";
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
// gColliderZoom removed, replaced by dynamic fitScale in drawColliderEditor

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
    if (!surface) return nullptr;
    SDL_Texture* texture = SDL_CreateTextureFromSurface(gRenderer, surface);
    SDL_DestroySurface(surface);
    return texture;
}

void loadTileTextures() {
    // Basic types
    gTileTypes = {
       {"Grass", 0, nullptr}, {"Dirt", 1, nullptr}, {"Stone", 2, nullptr},
       {"Sand", 3, nullptr}, {"Water", 4, nullptr}, {"Snow", 5, nullptr},
       {"Lava", 6, nullptr}, {"Wood", 7, nullptr}
    };

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
            map[x][y].scale = 1.0f; // Initialize scale
        }
    }
    return map;
}

Layer createLayer(int mapSize, const std::string& name, int layerIndex) {
    Layer layer;
    layer.name = name;
    layer.visible = true;

    if (layerIndex == 0) layer.map = generateMap(mapSize, 1);
    else if (layerIndex == 1) {
        layer.map = generateMap(mapSize, 0);
        layer.map[0][0].tileID = 3;
    }
    else layer.map = generateMap(mapSize, 0);

    return layer;
}

std::string getLevelPrefix(const Level& level, size_t index) {
    if (!level.name.empty()) return level.name;
    return "level_" + std::to_string(index + 1);
}

// --- Save/Load Logic (Updated for Scaling) ---
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
        if (!metaFile.is_open()) continue;

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
    }
    return true;
}

void saveTileColliders() {
    std::ofstream file(COLLIDER_SAVE_FILE);
    if (!file.is_open()) return;
    file << "tileID,isSolid,offsetLeft,offsetRight,offsetTop,offsetBottom\n";
    for (const auto& tile : gTileTypes) {
        file << (tile.tileID + 1) << "," << (tile.collider.isSolid ? "1" : "0") << ","
            << tile.collider.offsetLeft << "," << tile.collider.offsetRight << ","
            << tile.collider.offsetTop << "," << tile.collider.offsetBottom << "\n";
    }
    file.close();
}

void loadTileColliders() {
    std::ifstream file(COLLIDER_SAVE_FILE);
    if (!file.is_open()) return;
    std::string line;
    std::getline(file, line);
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        for (char& c : line) if (c == ',') c = ' ';
        std::stringstream ss(line);
        int rawID, isSolid, offL, offR, offT, offB;
        if (ss >> rawID >> isSolid >> offL >> offR >> offT >> offB) {
            int internalID = rawID - 1;
            for (auto& tile : gTileTypes) {
                if (tile.tileID == internalID) {
                    tile.collider = { (bool)isSolid, offL, offR, offT, offB };
                    break;
                }
            }
        }
    }
    file.close();
}

void addInitialLevels(int count) {
    if (gLevels.empty()) {
        for (int i = 0; i < count; ++i) {
            Level level;
            for (int j = 0; j < NUM_LAYERS; ++j) level.layers[j] = createLayer(gMapSize, LAYER_NAMES[j], j);
            gLevels.push_back(level);
        }
    }
}

void addNewLevel() {
    Level level;
    for (int j = 0; j < NUM_LAYERS; ++j) level.layers[j] = createLayer(gMapSize, LAYER_NAMES[j], j);
    gLevels.push_back(level);
    gCurrentLevelIndex = gLevels.size() - 1;
}

void deleteCurrentLevel() {
    if (gLevels.size() > 1) {
        gLevels.erase(gLevels.begin() + gCurrentLevelIndex);
        if (gCurrentLevelIndex >= gLevels.size()) gCurrentLevelIndex = gLevels.size() - 1;
    }
}

bool saveLevels() {
    std::cout << "[Action] Saving all levels..." << std::endl;
    saveTileColliders();
    saveMetaCSV();

    for (size_t i = 0; i < gLevels.size(); ++i) {
        const auto& level = gLevels[i];
        int mapSize = (int)level.layers[0].map.size();
        std::string prefix = getLevelPrefix(level, i);

        std::string metaFilename = SAVE_PATH + prefix + "_meta.csv";
        std::ofstream metaFile(metaFilename);
        if (metaFile.is_open()) {
            metaFile << "zoomLevel,mapOffsetX,mapOffsetY,mapSize,activeLayer,levelName\n";
            metaFile << level.zoomLevel << "," << level.mapOffsetX << "," << level.mapOffsetY << ","
                << mapSize << "," << level.activeLayer << "," << level.name << "\n";
            metaFile.close();
        }

        const char* layerSuffix[NUM_LAYERS] = { "terrain", "player", "furniture", "enemy", "npcs", "portal" };

        for (int layerIdx = 0; layerIdx < NUM_LAYERS; ++layerIdx) {
            const auto& layer = level.layers[layerIdx];

            // Save IDs
            std::string idFilename = SAVE_PATH + prefix + "_" + layerSuffix[layerIdx] + ".csv";
            std::ofstream idFile(idFilename);
            if (idFile.is_open()) {
                for (int y = 0; y < mapSize; ++y) {
                    for (int x = 0; x < mapSize; ++x) {
                        idFile << layer.map[x][y].tileID;
                        if (x < mapSize - 1) idFile << ",";
                    }
                    idFile << "\n";
                }
                idFile.close();
            }

            // Save Scales (NEW)
            std::string scaleFilename = SAVE_PATH + prefix + "_" + layerSuffix[layerIdx] + "_scale.csv";
            std::ofstream scaleFile(scaleFilename);
            if (scaleFile.is_open()) {
                scaleFile << std::fixed << std::setprecision(2);
                for (int y = 0; y < mapSize; ++y) {
                    for (int x = 0; x < mapSize; ++x) {
                        scaleFile << layer.map[x][y].scale;
                        if (x < mapSize - 1) scaleFile << ",";
                    }
                    scaleFile << "\n";
                }
                scaleFile.close();
            }
        }
    }
    std::cout << "[Status] Save successful." << std::endl;
    return true;
}

std::vector<std::string> split(const std::string& s, char delimiter) {
    std::vector<std::string> tokens;
    std::string token;
    std::istringstream tokenStream(s);
    while (std::getline(tokenStream, token, delimiter)) {
        tokens.push_back(token);
    }
    return tokens;
}

bool loadLevels() {
    std::cout << "[Action] Searching for level files..." << std::endl;
    gLevels.clear();
    int levelIndex = 1;

    while (true) {
        std::string checkFile = SAVE_PATH + "level_" + std::to_string(levelIndex) + "_metadata.csv";
        std::ifstream metaCheck(checkFile);
        if (!metaCheck.is_open()) break;
        metaCheck.close();

        Level level;
        std::string metaFilename = SAVE_PATH + "level_" + std::to_string(levelIndex) + "_meta.csv";
        std::ifstream lmf(metaFilename);
        if (lmf.is_open()) {
            std::string line;
            std::getline(lmf, line);
            if (std::getline(lmf, line)) {
                auto tokens = split(line, ',');
                if (tokens.size() >= 6) {
                    level.zoomLevel = std::stof(tokens[0]);
                    level.mapOffsetX = std::stof(tokens[1]);
                    level.mapOffsetY = std::stof(tokens[2]);
                    gMapSize = std::stoi(tokens[3]);
                    level.activeLayer = std::stoi(tokens[4]);
                    level.name = tokens[5];
                }
            }
            lmf.close();
        }

        level.targetOffsetX = level.mapOffsetX;
        level.targetOffsetY = level.mapOffsetY;
        const char* layerSuffix[NUM_LAYERS] = { "terrain", "player", "furniture", "enemy", "npcs", "portal" };

        for (int layerIdx = 0; layerIdx < NUM_LAYERS; ++layerIdx) {
            Layer layer;
            layer.name = LAYER_NAMES[layerIdx];
            layer.visible = true;
            layer.map = generateMap(gMapSize, 0);

            // Load IDs
            std::string idFilename = SAVE_PATH + "level_" + std::to_string(levelIndex) + "_" + layerSuffix[layerIdx] + ".csv";
            std::ifstream idFile(idFilename);
            if (idFile.is_open()) {
                std::string line;
                for (int y = 0; y < gMapSize; ++y) {
                    if (!std::getline(idFile, line)) break;
                    auto tokens = split(line, ',');
                    for (int x = 0; x < std::min((int)tokens.size(), gMapSize); ++x) {
                        layer.map[x][y].tileID = std::stoi(tokens[x]);
                    }
                }
                idFile.close();
            }

            // Load Scales (NEW)
            std::string scaleFilename = SAVE_PATH + "level_" + std::to_string(levelIndex) + "_" + layerSuffix[layerIdx] + "_scale.csv";
            std::ifstream scaleFile(scaleFilename);
            if (scaleFile.is_open()) {
                std::string line;
                for (int y = 0; y < gMapSize; ++y) {
                    if (!std::getline(scaleFile, line)) break;
                    auto tokens = split(line, ',');
                    for (int x = 0; x < std::min((int)tokens.size(), gMapSize); ++x) {
                        try {
                            layer.map[x][y].scale = std::stof(tokens[x]);
                        }
                        catch (...) { layer.map[x][y].scale = 1.0f; }
                    }
                }
                scaleFile.close();
            }
            level.layers[layerIdx] = layer;
        }

        gLevels.push_back(level);
        levelIndex++;
    }

    if (!gLevels.empty()) {
        gCurrentLevelIndex = 0;
        gGridSizeInput = std::to_string(gMapSize);
        std::cout << "[Status] " << gLevels.size() << " level(s) loaded." << std::endl;
        return true;
    }
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
    int targetX = std::max(0, std::min(mapSize - 1, (int)std::round(cart.x)));
    int targetY = std::max(0, std::min(mapSize - 1, (int)std::round(cart.y)));
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

// --- Initialization ---
bool init() {
    if (!SDL_Init(SDL_INIT_VIDEO)) return false;
    if (!TTF_Init()) return false;
    gWindow = SDL_CreateWindow("Osiris Level Editor (Updated)", INITIAL_SCREEN_WIDTH, INITIAL_SCREEN_HEIGHT, SDL_WINDOW_RESIZABLE);
    if (!gWindow) return false;
    gRenderer = SDL_CreateRenderer(gWindow, nullptr);
    if (!gRenderer) return false;
    gFont = TTF_OpenFont("assets/font/font.ttf", 16);
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

// --- FIX: FIXED COLLIDER EDITOR ---
void drawColliderEditor() {
    if (!gShowColliderEditor) return;
    int windowW, windowH;
    SDL_GetWindowSize(gWindow, &windowW, &windowH);

    SDL_SetRenderDrawColor(gRenderer, 0, 0, 0, 220);
    SDL_FRect fullscreen = { 0, 0, (float)windowW, (float)windowH };
    SDL_RenderFillRect(gRenderer, &fullscreen);

    if (gSelectedTileType < 0 || gSelectedTileType >= (int)gTileTypes.size()) return;
    TileType& tile = gTileTypes[gSelectedTileType];

    float editorBoxSize = 500.0f;
    float editorX = (windowW - editorBoxSize) / 2;
    float editorY = (windowH - editorBoxSize) / 2;

    renderText("COLLIDER EDITOR - Tile " + std::to_string(tile.tileID), editorX, editorY - 40, { 255, 255, 255, 255 });
    renderText("Press ESC to Close", editorX + 300, editorY - 40, { 180, 180, 180, 255 });

    SDL_FRect solidBtn = { editorX, editorY + editorBoxSize + 20, 200, 30 };
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

    float scaleX = editorBoxSize / texW;
    float scaleY = editorBoxSize / texH;
    float fitScale = std::min(scaleX, scaleY);
    if (fitScale < 1.0f) fitScale = 1.0f;
    fitScale = std::min(scaleX, scaleY);

    float displayW = texW * fitScale;
    float displayH = texH * fitScale;
    float displayX = editorX + (editorBoxSize - displayW) / 2;
    float displayY = editorY + (editorBoxSize - displayH) / 2;

    SDL_SetRenderDrawColor(gRenderer, 30, 30, 30, 255);
    SDL_FRect imgBg = { displayX, displayY, displayW, displayH };
    SDL_RenderFillRect(gRenderer, &imgBg);

    SDL_RenderTexture(gRenderer, tile.texture, nullptr, &imgBg);
    SDL_SetRenderDrawColor(gRenderer, 255, 255, 255, 255);
    SDL_RenderRect(gRenderer, &imgBg);

    if (fitScale > 4.0f) {
        SDL_SetRenderDrawColor(gRenderer, 100, 100, 100, 50);
        for (int i = 0; i <= (int)texW; i++) {
            float x = displayX + i * fitScale;
            SDL_RenderLine(gRenderer, x, displayY, x, displayY + displayH);
        }
        for (int i = 0; i <= (int)texH; i++) {
            float y = displayY + i * fitScale;
            SDL_RenderLine(gRenderer, displayX, y, displayX + displayW, y);
        }
    }

    if (tile.collider.isSolid) {
        float boxX = displayX + (tile.collider.offsetLeft * fitScale);
        float boxY = displayY + (tile.collider.offsetTop * fitScale);
        float boxW = displayW - ((tile.collider.offsetLeft + tile.collider.offsetRight) * fitScale);
        float boxH = displayH - ((tile.collider.offsetTop + tile.collider.offsetBottom) * fitScale);

        SDL_SetRenderDrawColor(gRenderer, 255, 0, 0, 80);
        SDL_FRect colRect = { boxX, boxY, boxW, boxH };
        SDL_RenderFillRect(gRenderer, &colRect);
        SDL_SetRenderDrawColor(gRenderer, 255, 0, 0, 255);
        SDL_RenderRect(gRenderer, &colRect);

        renderText("T: " + std::to_string(tile.collider.offsetTop), displayX + displayW + 10, displayY, { 255, 255, 0, 255 });
        renderText("B: " + std::to_string(tile.collider.offsetBottom), displayX + displayW + 10, displayY + displayH - 20, { 255, 255, 0, 255 });
        renderText("L: " + std::to_string(tile.collider.offsetLeft), displayX, displayY - 20, { 255, 255, 0, 255 });
        renderText("R: " + std::to_string(tile.collider.offsetRight), displayX + displayW - 20, displayY - 20, { 255, 255, 0, 255 });
    }
}

void handleColliderInput(SDL_Event& e) {
    if (!gShowColliderEditor) return;
    TileType& tile = gTileTypes[gSelectedTileType];

    if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE) {
        gShowColliderEditor = false;
        return;
    }

    if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_LEFT) {
        int windowW, windowH;
        SDL_GetWindowSize(gWindow, &windowW, &windowH);
        float editorBoxSize = 500.0f;
        float editorX = (windowW - editorBoxSize) / 2;
        float editorY = (windowH - editorBoxSize) / 2;

        SDL_FRect solidBtn = { editorX, editorY + editorBoxSize + 20, 200, 30 };
        if (e.button.x >= solidBtn.x && e.button.x <= solidBtn.x + solidBtn.w &&
            e.button.y >= solidBtn.y && e.button.y <= solidBtn.y + solidBtn.h) {
            tile.collider.isSolid = !tile.collider.isSolid;
            return;
        }

        if (!tile.collider.isSolid || !tile.texture) return;

        float texW, texH;
        SDL_GetTextureSize(tile.texture, &texW, &texH);
        float scaleX = editorBoxSize / texW;
        float scaleY = editorBoxSize / texH;
        float fitScale = std::min(scaleX, scaleY);

        float displayW = texW * fitScale;
        float displayH = texH * fitScale;
        float displayX = editorX + (editorBoxSize - displayW) / 2;
        float displayY = editorY + (editorBoxSize - displayH) / 2;

        if (e.button.x >= displayX && e.button.x <= displayX + displayW &&
            e.button.y >= displayY && e.button.y <= displayY + displayH) {

            float localX = (e.button.x - displayX) / fitScale;
            float localY = (e.button.y - displayY) / fitScale;

            float distToLeft = localX;
            float distToRight = texW - localX;
            float distToTop = localY;
            float distToBottom = texH - localY;

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

void drawTilePanel() {
    int windowW, windowH;
    SDL_GetWindowSize(gWindow, &windowW, &windowH);
    float panelX = (float)(windowW - TILE_PANEL_WIDTH);

    SDL_SetRenderDrawColor(gRenderer, 40, 40, 40, 255);
    SDL_FRect panelBg = { panelX, 0, (float)TILE_PANEL_WIDTH, (float)windowH };
    SDL_RenderFillRect(gRenderer, &panelBg);

    SDL_SetRenderDrawColor(gRenderer, 220, 220, 220, 255);
    renderText("All Tiles", panelX + 10, 15, { 220,220,220,255 });
    std::string scaleText = "Brush Scale: " + std::to_string((int)(gCurrentScale * 100)) + "%";
    renderText(scaleText, panelX + 10, 35, { 100, 255, 100, 255 });

    float tileY = 60;
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
            SDL_FRect highlight = { itemX - 5, itemY - 3, (float)TILE_PANEL_WIDTH - 20, tileSize + 6 };
            SDL_RenderFillRect(gRenderer, &highlight);
            renderText("[C] Edit Collider", itemX + 65, itemY + 42, { 255, 255, 0, 255 });
        }

        if (gTileTextures[i]) {
            float previewSize = 50;
            SDL_FRect dest = { itemX + 25 - previewSize / 2, itemY + tileSize / 2 - previewSize / 2, previewSize, previewSize };
            SDL_RenderTexture(gRenderer, gTileTextures[i], nullptr, &dest);
        }
        renderText("Tile " + std::to_string(i), itemX + 65, itemY + 22, { 220,220,220,255 });
    }
    SDL_SetRenderClipRect(gRenderer, nullptr);
}

// Updated drawTile to apply scale
void drawTile(int x, int y, const TileData& data, float tileWidth, float tileHeight, float mapOffsetX, float mapOffsetY, int alpha = 255) {
    SDL_FPoint screenPos = cartesianToScreen((float)x, (float)y, tileWidth, tileHeight, mapOffsetX, mapOffsetY);

    int windowW, windowH;
    SDL_GetWindowSize(gWindow, &windowW, &windowH);
    if (screenPos.x + tileWidth < 0 || screenPos.x - tileWidth > windowW ||
        screenPos.y + tileHeight < 0 || screenPos.y - tileHeight * 3 > windowH) return;

    SDL_Texture* tileTexture = nullptr;
    if (data.tileID >= 0 && data.tileID < (int)gTileTextures.size()) {
        tileTexture = gTileTextures[data.tileID];
    }

    if (tileTexture) {
        SDL_SetTextureAlphaMod(tileTexture, alpha);
        float texW, texH;
        SDL_GetTextureSize(tileTexture, &texW, &texH);

        // Scale application
        float scale = (tileWidth / BASE_TILE_WIDTH) * data.scale;

        float scaledWidth = texW * scale;
        float scaledHeight = texH * scale;

        SDL_FRect destRect = { screenPos.x - scaledWidth / 2.0f, screenPos.y - scaledHeight / 2.0f, scaledWidth, scaledHeight };
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
                if (layerIdx > 0 && layer.map[x][y].tileID == 0) continue;
                drawTile(x, y, layer.map[x][y], tileWidth, tileHeight, currentLevel.mapOffsetX, currentLevel.mapOffsetY, alpha);
            }
        }
    }
}

// Merged drawOverlay: Dynamic Grid Box + Restored Level Info
void drawOverlay() {
    int windowW, windowH;
    SDL_GetWindowSize(gWindow, &windowW, &windowH);
    SDL_Color textColor = { 220, 220, 220, 255 };

    // --- Top Right: Grid Size (Dynamic) ---
    float uiWidth = 200;
    float uiX = (float)windowW - uiWidth - 10 - TILE_PANEL_WIDTH;

    SDL_SetRenderDrawColor(gRenderer, 20, 20, 20, 180);
    SDL_FRect labelBg = { uiX, 10, uiWidth, 35 };
    SDL_RenderFillRect(gRenderer, &labelBg);
    renderText("Grid Size:", uiX + 10, 18, textColor);

    std::string displayText = (gIsEditingGridSize && !gGridSizeInput.empty() ? gGridSizeInput : std::to_string(gMapSize)) + "x" +
        (gIsEditingGridSize && !gGridSizeInput.empty() ? gGridSizeInput : std::to_string(gMapSize));

    int textW = 0, textH = 0;
    if (gFont) TTF_GetStringSizeWrapped(gFont, displayText.c_str(), 0, 0, &textW, &textH);
    else textW = (int)displayText.length() * 8;

    float inputW = std::max(60.0f, (float)textW + 20.0f);
    float inputX = uiX + 100;
    SDL_FRect valueRect = { inputX, 15, inputW, 25 };

    if (gIsEditingGridSize) SDL_SetRenderDrawColor(gRenderer, 100, 150, 255, 255);
    else SDL_SetRenderDrawColor(gRenderer, 60, 60, 60, 255);

    SDL_RenderFillRect(gRenderer, &valueRect);
    SDL_SetRenderDrawColor(gRenderer, 240, 240, 240, 255);
    SDL_RenderRect(gRenderer, &valueRect);
    renderText(displayText, valueRect.x + 5, valueRect.y + 5, textColor);

    // --- Top Left: Level Name (Restored) ---
    SDL_SetRenderDrawColor(gRenderer, 20, 20, 20, 180);
    SDL_FRect nameBgRect = { 10, 10, 260, 35 };
    SDL_RenderFillRect(gRenderer, &nameBgRect);
    renderText("Level Name:", 20, 18, textColor);

    float nameLabelW = 95;
    SDL_FRect nameValueRect = { 20 + nameLabelW + 5, 15, 145, 25 };

    if (gIsEditingLevelName) SDL_SetRenderDrawColor(gRenderer, 100, 150, 255, 255);
    else SDL_SetRenderDrawColor(gRenderer, 60, 60, 60, 255);

    SDL_RenderFillRect(gRenderer, &nameValueRect);
    SDL_SetRenderDrawColor(gRenderer, 240, 240, 240, 255);
    SDL_RenderRect(gRenderer, &nameValueRect);

    std::string displayName;
    if (gIsEditingLevelName) displayName = gLevelNameInput;
    else if (!gLevels.empty() && !gLevels[gCurrentLevelIndex].name.empty()) displayName = gLevels[gCurrentLevelIndex].name;
    else displayName = "(default)";
    renderText(displayName, nameValueRect.x + 5, nameValueRect.y + 5, textColor);

    if (gIsEditingLevelName && SDL_GetTicks() % 1000 < 500) {
        int cursorW = 0, cursorH = 0;
        if (gFont && !gLevelNameInput.empty()) TTF_GetStringSizeWrapped(gFont, gLevelNameInput.c_str(), 0, 0, &cursorW, &cursorH);
        else cursorW = (int)gLevelNameInput.length() * 8;
        float cursorX = nameValueRect.x + 5 + (float)cursorW;
        SDL_SetRenderDrawColor(gRenderer, 255, 255, 255, 255);
        SDL_RenderLine(gRenderer, cursorX, nameValueRect.y + 5, cursorX, nameValueRect.y + 20);
    }

    // --- Bottom Left: Level/Layer Info (Restored) ---
    if (!gLevels.empty()) {
        Level& currentLevel = gLevels[gCurrentLevelIndex];

        std::string levelText = "Level: " + std::to_string(gCurrentLevelIndex + 1) + " / " + std::to_string(gLevels.size());
        int levelTextW = 0;
        if (gFont) TTF_GetStringSizeWrapped(gFont, levelText.c_str(), 0, 0, &levelTextW, &textH);
        else levelTextW = (int)levelText.length() * 8;

        SDL_SetRenderDrawColor(gRenderer, 20, 20, 20, 180);
        SDL_FRect bgRect = { 10, (float)windowH - 35, (float)levelTextW + 10.0f, 25 };
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

void handleEvents(bool& quit) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_EVENT_QUIT) { quit = true; return; }
        if (gShowColliderEditor) { handleColliderInput(e); continue; }

        if (e.type == SDL_EVENT_KEY_DOWN) {
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
            else if (gIsEditingGridSize) {
                if (e.key.key == SDLK_RETURN || e.key.key == SDLK_KP_ENTER) {
                    if (!gGridSizeInput.empty()) {
                        try {
                            int newSize = std::stoi(gGridSizeInput);
                            if (newSize >= 5 && newSize <= 500) {
                                gMapSize = newSize;
                                for (auto& level : gLevels) {
                                    for (int i = 0; i < NUM_LAYERS; ++i)
                                        level.layers[i] = createLayer(gMapSize, LAYER_NAMES[i], i);
                                }
                            }
                        }
                        catch (...) {}
                    }
                    gIsEditingGridSize = false; SDL_StopTextInput(gWindow);
                }
                else if (e.key.key == SDLK_BACKSPACE && !gGridSizeInput.empty()) gGridSizeInput.pop_back();
                else if (e.key.key == SDLK_ESCAPE) {
                    gGridSizeInput = std::to_string(gMapSize);
                    gIsEditingGridSize = false;
                    SDL_StopTextInput(gWindow);
                }
            }
            else {
                if (e.key.key == SDLK_S && (e.key.mod & SDL_KMOD_CTRL)) saveLevels();
                else if (e.key.key == SDLK_N && (e.key.mod & SDL_KMOD_CTRL)) addNewLevel();
                else if (e.key.key == SDLK_D && (e.key.mod & SDL_KMOD_CTRL)) deleteCurrentLevel();
                else if (e.key.key == SDLK_LEFT && gCurrentLevelIndex > 0) gCurrentLevelIndex--;
                else if (e.key.key == SDLK_RIGHT && gCurrentLevelIndex < gLevels.size() - 1) gCurrentLevelIndex++;
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

                // Scaling Hotkeys (NEW)
                else if (e.key.key == SDLK_LEFTBRACKET) {
                    gCurrentScale -= 0.1f;
                    if (gCurrentScale < 0.1f) gCurrentScale = 0.1f;
                    std::cout << "Brush Scale: " << gCurrentScale << std::endl;
                }
                else if (e.key.key == SDLK_RIGHTBRACKET) {
                    gCurrentScale += 0.1f;
                    std::cout << "Brush Scale: " << gCurrentScale << std::endl;
                }
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
                if (e.text.text[0] >= '0' && e.text.text[0] <= '9') gGridSizeInput += e.text.text;
            }
        }
        else if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
            if (e.button.button == SDL_BUTTON_LEFT) {
                int windowW, windowH;
                SDL_GetWindowSize(gWindow, &windowW, &windowH);
                float panelX = (float)(windowW - TILE_PANEL_WIDTH);

                // Click Level Name Input
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

                // Click Grid Size Input (Dynamic Hit Test)
                float uiWidth = 200;
                float uiX = (float)windowW - uiWidth - 10 - TILE_PANEL_WIDTH;
                if (e.button.x > uiX && e.button.y < 50) {
                    gIsEditingGridSize = true;
                    gIsEditingLevelName = false;
                    gGridSizeInput = "";
                    SDL_StartTextInput(gWindow);
                    continue;
                }

                // Tile Panel Click
                if (e.button.x >= panelX) {
                    float tileY = 60, tileSize = 60, spacing = 8;
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
                // Map Area Click
                else {
                    gIsEditingGridSize = false;
                    gIsEditingLevelName = false;
                    SDL_StopTextInput(gWindow);
                    gMouseDownX = (float)e.button.x;
                    gMouseDownY = (float)e.button.y;

                    if (!gLevels.empty()) {
                        Level& currentLevel = gLevels[gCurrentLevelIndex];
                        int mapSize = (int)currentLevel.layers[0].map.size();
                        float tileWidth, tileHeight;
                        updateTileSizes(currentLevel.zoomLevel, tileWidth, tileHeight);
                        SDL_FPoint cart = screenToCartesian((float)e.button.x, (float)e.button.y, tileWidth, tileHeight, currentLevel.mapOffsetX, currentLevel.mapOffsetY);
                        int gx = (int)std::round(cart.x);
                        int gy = (int)std::round(cart.y);
                        if (gx >= 0 && gx < mapSize && gy >= 0 && gy < mapSize) {
                            currentLevel.layers[currentLevel.activeLayer].map[gx][gy].tileID = gSelectedTileType;
                            // Apply Scale on Click
                            currentLevel.layers[currentLevel.activeLayer].map[gx][gy].scale = gCurrentScale;
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
                float tileY = 60;
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
                            // Apply Scale on Drag
                            currentLevel.layers[currentLevel.activeLayer].map[gridX][gridY].scale = gCurrentScale;
                        }
                    }
                }
            }
        }
        // Restored Mouse Wheel Logic
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