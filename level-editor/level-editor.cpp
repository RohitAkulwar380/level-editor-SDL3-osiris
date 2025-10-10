#include <SDL3/SDL.h>
#include <iostream>
#include <vector>
#include <cmath>
#include <algorithm>
#include <string>
#include <fstream>
#include <sstream> 
#include <cstdio> 

// --- Configuration and Constants ---
const int INITIAL_SCREEN_WIDTH = 1280;
const int INITIAL_SCREEN_HEIGHT = 720;
int gMapSize = 40;
const float BASE_TILE_WIDTH = 128.0f;

const std::string SAVE_PATH = "D:\\codingStudy\\IMED MCA\\Sem - 3\\Project\\level-editor-SDL3-osiris\\assets\\levels\\";

// --- Data Structures ---
struct TileData {
    int height;
    SDL_Color color;
};

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
std::vector<Level> gLevels;
size_t gCurrentLevelIndex = 0;
bool gIsDragging = false;
float gLastMouseX = 0.0f;
float gLastMouseY = 0.0f;
bool gIsSnapping = false;
const float SNAP_SPEED = 0.1f;

// --- Utility Functions ---
void RenderFillPolygon(SDL_Renderer* renderer, const SDL_FPoint* vertices, int count) {
    if (count != 4) return;
    SDL_Vertex render_vertices[4];
    SDL_Color color;
    SDL_GetRenderDrawColor(renderer, &color.r, &color.g, &color.b, &color.a);
    for (int i = 0; i < count; ++i) {
        render_vertices[i].position = vertices[i];
        render_vertices[i].color = { (float)color.r / 255.0f, (float)color.g / 255.0f, (float)color.b / 255.0f, (float)color.a / 255.0f };
        render_vertices[i].tex_coord = { 0.0f, 0.0f };
    }
    const int indices[6] = { 0, 1, 2, 0, 2, 3 };
    SDL_RenderGeometry(renderer, nullptr, render_vertices, count, indices, 6);
}

// FIX #1: The function signature is corrected to accept a size parameter.
std::vector<std::vector<TileData>> generateMap(int mapSize) {
    std::vector<std::vector<TileData>> map;
    map.resize(mapSize, std::vector<TileData>(mapSize));
    for (int x = 0; x < mapSize; ++x) {
        for (int y = 0; y < mapSize; ++y) {
            map[x][y].height = 0;
            map[x][y].color = { 60, 180, 120, 255 };
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
        int mapSize = level.map.size();
        file << level.zoomLevel << "," << level.mapOffsetX << "," << level.mapOffsetY << "," << mapSize << "\n";
        for (int y = 0; y < mapSize; ++y) {
            for (int x = 0; x < mapSize; ++x) {
                file << level.map[x][y].height << (x < mapSize - 1 ? "," : "");
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
        if (!file.is_open()) break;

        Level level;
        std::string line, value;

        if (!std::getline(file, line)) break;
        std::stringstream header_ss(line);
        std::getline(header_ss, value, ','); level.zoomLevel = std::stof(value);
        std::getline(header_ss, value, ','); level.mapOffsetX = std::stof(value);
        std::getline(header_ss, value, ','); level.mapOffsetY = std::stof(value);

        int mapSize = 0;
        if (std::getline(header_ss, value, ',')) {
            mapSize = std::stoi(value);
        }
        else {
            mapSize = 40;
        }

        level.targetOffsetX = level.mapOffsetX;
        level.targetOffsetY = level.mapOffsetY;

        level.map = generateMap(mapSize);
        for (int y = 0; y < mapSize; ++y) {
            if (!std::getline(file, line)) break;
            std::stringstream map_row_ss(line);
            for (int x = 0; x < mapSize; ++x) {
                if (!std::getline(map_row_ss, value, ',')) break;
                level.map[x][y].height = std::stoi(value);
            }
        }
        gLevels.push_back(level);
        levelIndex++;
    }
    if (!gLevels.empty()) {
        gCurrentLevelIndex = 0;
        gMapSize = gLevels[0].map.size();
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
    return { (float)windowW / 2.0f, (float)windowH / 2.0f };
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
    int mapSize = currentLevel.map.size();
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
    if (SDL_Init(SDL_INIT_VIDEO) < 0) return false;
    gWindow = SDL_CreateWindow("Osiris Level Editor (SDL3)", INITIAL_SCREEN_WIDTH, INITIAL_SCREEN_HEIGHT, SDL_WINDOW_RESIZABLE);
    if (gWindow == nullptr) return false;
    gRenderer = SDL_CreateRenderer(gWindow, nullptr);
    if (gRenderer == nullptr) return false;
    SDL_SetRenderDrawBlendMode(gRenderer, SDL_BLENDMODE_BLEND);
    return true;
}
void close() {
    saveLevels();
    SDL_DestroyRenderer(gRenderer);
    SDL_DestroyWindow(gWindow);
    SDL_Quit();
}

// --- Rendering Functions ---
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
    SDL_FPoint vertices[4] = { {px, py - height_offset}, {px + tileWidth / 2.0f, py + tileHeight / 2.0f - height_offset}, {px, py + tileHeight - height_offset}, {px - tileWidth / 2.0f, py + tileHeight / 2.0f - height_offset} };
    SDL_SetRenderDrawColor(gRenderer, data.color.r, data.color.g, data.color.b, 255);
    RenderFillPolygon(gRenderer, vertices, 4);
    SDL_SetRenderDrawColor(gRenderer, 30, 80, 50, 255);
    SDL_FPoint borderVertices[5] = { vertices[0], vertices[1], vertices[2], vertices[3], vertices[0] };
    SDL_RenderLines(gRenderer, borderVertices, 5);
    if (h > 0) {
        SDL_FPoint basePoint = { px, py + tileHeight };
        SDL_FPoint baseLeft = { px - tileWidth / 2.0f, py + tileHeight / 2.0f };
        SDL_FPoint baseRight = { px + tileWidth / 2.0f, py + tileHeight / 2.0f };
        SDL_SetRenderDrawColor(gRenderer, 80, 160, 120, 255);
        SDL_FPoint sideVerticesLeft[4] = { vertices[3], vertices[2], basePoint, baseLeft };
        RenderFillPolygon(gRenderer, sideVerticesLeft, 4);
        SDL_SetRenderDrawColor(gRenderer, 40, 120, 90, 255);
        SDL_FPoint sideVerticesRight[4] = { vertices[1], vertices[2], basePoint, baseRight };
        RenderFillPolygon(gRenderer, sideVerticesRight, 4);
    }
}

void drawOverlay() {
    int windowW, windowH;
    SDL_GetWindowSize(gWindow, &windowW, &windowH);
    if (!gLevels.empty()) {
        std::string text = "Level: " + std::to_string(gCurrentLevelIndex + 1) + " / " + std::to_string(gLevels.size());
        SDL_SetRenderDrawColor(gRenderer, 20, 20, 20, 180);
        SDL_FRect bgRect = { 10, (float)windowH - 35, (float)text.length() * 8.0f + 10.0f, 25 };
        SDL_RenderFillRect(gRenderer, &bgRect);
        SDL_SetRenderDrawColor(gRenderer, 220, 220, 220, 255);
        SDL_FRect textRect = { 15, (float)windowH - 30, (float)text.length() * 8.0f, 15 };
        SDL_RenderFillRect(gRenderer, &textRect);
    }
    float uiWidth = 250;
    float uiX = (float)windowW - uiWidth - 10;
    SDL_SetRenderDrawColor(gRenderer, 20, 20, 20, 180);
    SDL_FRect bgRect = { uiX, 10, uiWidth, 35 };
    SDL_RenderFillRect(gRenderer, &bgRect);
    SDL_SetRenderDrawColor(gRenderer, 220, 220, 220, 255);
    SDL_FRect labelRect = { uiX + 10, 20, 80, 15 };
    SDL_RenderFillRect(gRenderer, &labelRect);
    std::string sizeText = std::to_string(gMapSize) + "x" + std::to_string(gMapSize);
    SDL_FRect valueRect = { labelRect.x + labelRect.w + 5, 15, 60, 25 };
    SDL_RenderFillRect(gRenderer, &valueRect);
    SDL_SetRenderDrawColor(gRenderer, 200, 80, 80, 255);
    SDL_FRect minusRect = { valueRect.x + valueRect.w + 10, 15, 25, 25 };
    SDL_RenderFillRect(gRenderer, &minusRect);
    SDL_SetRenderDrawColor(gRenderer, 80, 180, 80, 255);
    SDL_FRect plusRect = { minusRect.x + minusRect.w + 5, 15, 25, 25 };
    SDL_RenderFillRect(gRenderer, &plusRect);
}

void draw() {
    SDL_SetRenderDrawColor(gRenderer, 31, 41, 55, 255);
    SDL_RenderClear(gRenderer);
    if (!gLevels.empty()) {
        Level& currentLevel = gLevels[gCurrentLevelIndex];
        int mapSize = currentLevel.map.size();
        float tileWidth, tileHeight;
        updateTileSizes(currentLevel.zoomLevel, tileWidth, tileHeight);
        for (int y = 0; y < mapSize; ++y) {
            for (int x = 0; x < mapSize; ++x) {
                drawTile(x, y, currentLevel.map[x][y], tileWidth, tileHeight, currentLevel.mapOffsetX, currentLevel.mapOffsetY);
            }
        }
    }
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

        if (event.type == SDL_EVENT_MOUSE_BUTTON_UP && event.button.button == SDL_BUTTON_LEFT) {
            int windowW, windowH;
            SDL_GetWindowSize(gWindow, &windowW, &windowH);
            float uiWidth = 250;
            float uiX = (float)windowW - uiWidth - 10;
            SDL_FRect valueRect = { uiX + 10 + 80 + 5, 15, 60, 25 };
            SDL_FRect minusRect = { valueRect.x + valueRect.w + 10, 15, 25, 25 };
            SDL_FRect plusRect = { minusRect.x + minusRect.w + 5, 15, 25, 25 };

            if (event.button.y >= 10 && event.button.y <= 45) {
                bool sizeChanged = false;
                if (event.button.x >= minusRect.x && event.button.x <= minusRect.x + minusRect.w) {
                    if (gMapSize > 5) { gMapSize--; sizeChanged = true; }
                }
                else if (event.button.x >= plusRect.x && event.button.x <= plusRect.x + plusRect.w) {
                    if (gMapSize < 100) { gMapSize++; sizeChanged = true; }
                }
                if (sizeChanged && !gLevels.empty()) {
                    std::cout << "[Action] Grid size set to " << gMapSize << ". Resizing and resetting current level." << std::endl;
                    gLevels[gCurrentLevelIndex].map = generateMap(gMapSize);
                    snapToNearestTile();
                    return;
                }
            }
        }

        if (gLevels.empty()) {
            // FIX #2: Corrected the typo from "scancancode" to "scancode".
            if (event.type == SDL_EVENT_KEY_DOWN && event.key.scancode == SDL_SCANCODE_N) {
                addNewLevel();
                snapToNearestTile();
            }
            continue;
        }

        Level& currentLevel = gLevels[gCurrentLevelIndex];
        int mapSize = currentLevel.map.size();
        switch (event.type) {
        case SDL_EVENT_KEY_DOWN:
            switch (event.key.scancode) {
            case SDL_SCANCODE_S: saveLevels(); break;
            case SDL_SCANCODE_RIGHT:
                if (gCurrentLevelIndex < gLevels.size() - 1) {
                    gCurrentLevelIndex++;
                    gMapSize = gLevels[gCurrentLevelIndex].map.size();
                    std::cout << "[Action] Switched to Level " << gCurrentLevelIndex + 1 << " (" << gMapSize << "x" << gMapSize << ")." << std::endl;
                    snapToNearestTile();
                }
                break;
            case SDL_SCANCODE_LEFT:
                if (gCurrentLevelIndex > 0) {
                    gCurrentLevelIndex--;
                    gMapSize = gLevels[gCurrentLevelIndex].map.size();
                    std::cout << "[Action] Switched to Level " << gCurrentLevelIndex + 1 << " (" << gMapSize << "x" << gMapSize << ")." << std::endl;
                    snapToNearestTile();
                }
                break;
            case SDL_SCANCODE_N: addNewLevel(); snapToNearestTile(); break;
            case SDL_SCANCODE_DELETE: deleteCurrentLevel(); snapToNearestTile(); break;
            case SDL_SCANCODE_R:
                gLevels[gCurrentLevelIndex].map = generateMap(gLevels[gCurrentLevelIndex].map.size());
                snapToNearestTile();
                std::cout << "[Status] Level " << gCurrentLevelIndex + 1 << " has been reset to a flat grid." << std::endl;
                break;
            default: break;
            }
            break;
        case SDL_EVENT_MOUSE_WHEEL: {
            float mouseX, mouseY;
            SDL_GetMouseState(&mouseX, &mouseY);
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
            break;
        }
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            if (event.button.button == SDL_BUTTON_LEFT) {
                gIsDragging = true;
                gIsSnapping = false;
                gLastMouseX = event.button.x;
                gLastMouseY = event.button.y;
            }
            break;
        case SDL_EVENT_MOUSE_BUTTON_UP:
            if (event.button.button == SDL_BUTTON_LEFT) {
                bool wasDragging = gIsDragging;
                gIsDragging = false;
                if (!wasDragging) {
                    float tileWidth, tileHeight;
                    updateTileSizes(currentLevel.zoomLevel, tileWidth, tileHeight);
                    SDL_FPoint cart = screenToCartesian((float)event.button.x, (float)event.button.y, tileWidth, tileHeight, currentLevel.mapOffsetX, currentLevel.mapOffsetY);
                    int mapX = (int)std::round(cart.x);
                    int mapY = (int)std::round(cart.y);
                    if (mapX >= 0 && mapX < mapSize && mapY >= 0 && mapY < mapSize) {
                        if (SDL_GetModState() & SDL_KMOD_LCTRL) {
                            currentLevel.map[mapX][mapY].height = std::max(0, currentLevel.map[mapX][mapY].height - 1);
                        }
                        else {
                            currentLevel.map[mapX][mapY].height = std::min(10, currentLevel.map[mapX][mapY].height + 1);
                        }
                    }
                }
            }
            break;
        case SDL_EVENT_MOUSE_MOTION:
            if (gIsDragging) {
                currentLevel.mapOffsetX += event.motion.x - gLastMouseX;
                currentLevel.mapOffsetY += event.motion.y - gLastMouseY;
                currentLevel.targetOffsetX = currentLevel.mapOffsetX;
                currentLevel.targetOffsetY = currentLevel.mapOffsetY;
                gLastMouseX = event.motion.x;
                gLastMouseY = event.motion.y;
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