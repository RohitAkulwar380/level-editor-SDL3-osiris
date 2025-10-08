#include <SDL3/SDL.h>
#include <iostream>
#include <vector>
#include <cmath>
#include <algorithm>

// --- Configuration and Constants ---
// NOTE: SCREEN_WIDTH/HEIGHT globals are only for initial window creation size.
const int INITIAL_SCREEN_WIDTH = 1280;
const int INITIAL_SCREEN_HEIGHT = 720;
const int MAP_SIZE = 15;
const float BASE_TILE_WIDTH = 128.0f;

// --- Global State ---
SDL_Window* gWindow = nullptr;
SDL_Renderer* gRenderer = nullptr;

struct TileData {
    int height;
    SDL_Color color;
};

// Map state
std::vector<std::vector<TileData>> gMap;

// Camera state
float gZoomLevel = 1.0f;
float gTileWidth = BASE_TILE_WIDTH;
float gTileHeight = BASE_TILE_WIDTH / 2.0f;
float gMapOffsetX = 0.0f;
float gMapOffsetY = 0.0f;

// Panning state
bool gIsDragging = false;
float gLastMouseX = 0.0f;
float gLastMouseY = 0.0f;

// Snapping state (for smooth movement)
float gTargetOffsetX = 0.0f;
float gTargetOffsetY = 0.0f;
bool gIsSnapping = false;
const float SNAP_SPEED = 0.1f; // Lerp factor for smooth snapping

// --- Utility Functions (SDL Initialization and Cleanup) ---

bool init() {
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        std::cerr << "SDL could not initialize! SDL_Error: " << SDL_GetError() << std::endl;
        return false;
    }

    gWindow = SDL_CreateWindow("Isometric Map with Pan & Snap (SDL3)", INITIAL_SCREEN_WIDTH, INITIAL_SCREEN_HEIGHT, SDL_WINDOW_RESIZABLE);
    if (gWindow == nullptr) {
        std::cerr << "Window could not be created! SDL_Error: " << SDL_GetError() << std::endl;
        return false;
    }

    gRenderer = SDL_CreateRenderer(gWindow, nullptr);
    if (gRenderer == nullptr) {
        std::cerr << "Renderer could not be created! SDL_Error: " << SDL_GetError() << std::endl;
        return false;
    }

    SDL_SetRenderDrawBlendMode(gRenderer, SDL_BLENDMODE_BLEND);
    return true;
}

void close() {
    SDL_DestroyRenderer(gRenderer);
    SDL_DestroyWindow(gWindow);
    SDL_Quit();
}

/**
 * Custom helper for drawing filled quadrilaterals (diamonds) using SDL_RenderGeometry.
 * This function handles the conversion from SDL_Color (0-255) to SDL_FColor (0.0-1.0)
 * required by SDL_RenderGeometry, resolving the C2679 error.
 */
void RenderFillPolygon(SDL_Renderer* renderer, const SDL_FPoint* vertices, int count) {
    if (count != 4) return; // Only supports 4-sided diamond for this map

    // Vertices: V0, V1, V2, V3
    // Draw the quadrilateral as two triangles: (V0, V1, V2) and (V0, V2, V3)

    SDL_Vertex render_vertices[4];
    SDL_Color color;
    // Get the current draw color (in 0-255 range)
    SDL_GetRenderDrawColor(renderer, &color.r, &color.g, &color.b, &color.a);

    for (int i = 0; i < count; ++i) {
        render_vertices[i].position = vertices[i];

        // Convert SDL_Color (uint8_t 0-255) to SDL_FColor (float 0.0-1.0)
        render_vertices[i].color = {
            (float)color.r / 255.0f,
            (float)color.g / 255.0f,
            (float)color.b / 255.0f,
            (float)color.a / 255.0f
        };

        render_vertices[i].tex_coord = { 0.0f, 0.0f }; // No texture
    }

    // Indices for the two triangles
    const int indices[6] = {
        0, 1, 2, // Triangle 1 (Top, Right, Bottom)
        0, 2, 3  // Triangle 2 (Top, Bottom, Left)
    };

    // Render the triangles
    SDL_RenderGeometry(renderer, nullptr, render_vertices, count, indices, 6);
}

// --- Map Logic ---

void generateMap() {
    gMap.resize(MAP_SIZE, std::vector<TileData>(MAP_SIZE));
    for (int x = 0; x < MAP_SIZE; ++x) {
        for (int y = 0; y < MAP_SIZE; ++y) {
            gMap[x][y].height = rand() % 3; // 0, 1, or 2
            gMap[x][y].color = { 60, 180, 120, 255 }; // Teal/Green color
        }
    }
}

// Update tile size based on current zoom level
void updateTileSizes() {
    gTileWidth = BASE_TILE_WIDTH * gZoomLevel;
    gTileHeight = gTileWidth / 2.0f;
}

// Convert cartesian (x, y) coordinates to screen (px, py) coordinates
SDL_FPoint cartesianToScreen(float x, float y) {
    float px = gMapOffsetX + (x - y) * gTileWidth / 2.0f;
    float py = gMapOffsetY + (x + y) * gTileHeight / 2.0f;
    return { px, py };
}

// Convert screen (px, py) coordinates to cartesian (x, y) coordinates (Inverse Projection)
SDL_FPoint screenToCartesian(float px, float py) {
    // Coords relative to map origin (0,0) before offset
    float relativeX = px - gMapOffsetX;
    float relativeY = py - gMapOffsetY;

    // Inverse Isometric Projection:
    float x = (relativeY / gTileHeight) + (relativeX / gTileWidth);
    float y = (relativeY / gTileHeight) - (relativeX / gTileWidth);

    return { x, y };
}

void drawTile(int x, int y, const TileData& data) {
    SDL_FPoint screenPos = cartesianToScreen((float)x, (float)y);
    float px = screenPos.x;
    float py = screenPos.y;
    int h = data.height;

    // Get current window size for culling optimization
    int windowW, windowH;
    SDL_GetWindowSize(gWindow, &windowW, &windowH);

    // Optimization: Don't draw if completely off-screen
    if (px + gTileWidth < 0 || px - gTileWidth > windowW ||
        py + gTileHeight < 0 || py - gTileHeight > windowH) {
        return;
    }

    // Define the diamond shape vertices for the top surface
    SDL_FPoint vertices[4];
    float height_offset = h * gZoomLevel * 5.0f; // Height visual based on zoom

    // 1. Top Point
    vertices[0] = { px, py - height_offset };
    // 2. Right Point
    vertices[1] = { px + gTileWidth / 2.0f, py + gTileHeight / 2.0f - height_offset };
    // 3. Bottom Point
    vertices[2] = { px, py + gTileHeight - height_offset };
    // 4. Left Point
    vertices[3] = { px - gTileWidth / 2.0f, py + gTileHeight / 2.0f - height_offset };

    // --- Draw the Top Surface (using the custom RenderFillPolygon) ---
    SDL_Color baseColor = data.color;
    SDL_SetRenderDrawColor(gRenderer, baseColor.r, baseColor.g, baseColor.b, 255);
    RenderFillPolygon(gRenderer, vertices, 4);

    // --- Draw Border ---
    SDL_FPoint borderVertices[5] = { vertices[0], vertices[1], vertices[2], vertices[3], vertices[0] };

    SDL_SetRenderDrawColor(gRenderer, 30, 80, 50, 255); // Darker border color
    SDL_RenderLines(gRenderer, borderVertices, 5); // Draw the diamond border

    // --- Draw the sides (optional but good for 3D feel) ---
    if (h > 0) {
        SDL_SetRenderDrawColor(gRenderer, 80, 200, 140, 255); // Lighter side color
        SDL_FPoint sideVerticesLeft[4];
        // Left side vertices: V3(Top-Left), V2(Top-Bottom), V2_base, V3_base
        sideVerticesLeft[0] = vertices[3]; // Top-Left
        sideVerticesLeft[1] = vertices[2]; // Top-Bottom
        sideVerticesLeft[2] = { px, py + gTileHeight + height_offset }; // Bottom-Bottom (at base height)
        sideVerticesLeft[3] = { px - gTileWidth / 2.0f, py + gTileHeight / 2.0f + height_offset }; // Bottom-Left (at base height)
        RenderFillPolygon(gRenderer, sideVerticesLeft, 4);

        SDL_SetRenderDrawColor(gRenderer, 80, 200, 140, 255); // Ensure color is set before drawing right side
        // Right side vertices: V1(Top-Right), V2(Top-Bottom), V2_base, V1_base
        SDL_FPoint sideVerticesRight[4];
        sideVerticesRight[0] = vertices[1]; // Top-Right
        sideVerticesRight[1] = vertices[2]; // Top-Bottom
        sideVerticesRight[2] = { px, py + gTileHeight + height_offset }; // Bottom-Bottom (at base height)
        sideVerticesRight[3] = { px + gTileWidth / 2.0f, py + gTileHeight / 2.0f + height_offset }; // Bottom-Right (at base height)
        RenderFillPolygon(gRenderer, sideVerticesRight, 4);
    }
}

void draw() {
    // Clear screen (Dark blue background)
    SDL_SetRenderDrawColor(gRenderer, 31, 41, 55, 255);
    SDL_RenderClear(gRenderer);

    updateTileSizes();

    // Draw map tiles in order (back to front: top-left to bottom-right)
    for (int x = 0; x < MAP_SIZE; ++x) {
        for (int y = 0; y < MAP_SIZE; ++y) {
            drawTile(x, y, gMap[x][y]);
        }
    }

    // Present the renderer
    SDL_RenderPresent(gRenderer);
}

// --- Snapping Logic ---

void snapToNearestTile() {
    // 1. Get the screen center coordinates
    int windowW, windowH;
    // NOTE: SDL_GetWindowSize expects non-const int pointers.
    SDL_GetWindowSize(gWindow, &windowW, &windowH);
    float centerX = windowW / 2.0f;
    float centerY = windowH / 2.0f;

    // 2. Find the tile currently at the center of the screen (in floating-point Cartesian)
    SDL_FPoint centerCart = screenToCartesian(centerX, centerY);

    // 3. Determine the target tile (round to nearest integer)
    int snappedX = std::max(0, std::min(MAP_SIZE - 1, (int)std::round(centerCart.x)));
    int snappedY = std::max(0, std::min(MAP_SIZE - 1, (int)std::round(centerCart.y)));

    // 4. Calculate the required new map offset to center that tile
    // Calculate the screen position of the target tile if map offset was (0,0)
    float targetOriginX = (snappedX - snappedY) * gTileWidth / 2.0f;
    float targetOriginY = (snappedX + snappedY) * gTileHeight / 2.0f;

    // New offset is the difference needed to move the target tile's screen position to the center
    gTargetOffsetX = centerX - targetOriginX;
    gTargetOffsetY = centerY - targetOriginY;

    // Start snapping animation
    gIsSnapping = true;
}

// Update the camera offset smoothly towards the target offset
void updateCamera() {
    if (!gIsSnapping) return;

    // Linear interpolation (Lerp) for smooth movement
    gMapOffsetX = gMapOffsetX + (gTargetOffsetX - gMapOffsetX) * SNAP_SPEED;
    gMapOffsetY = gMapOffsetY + (gTargetOffsetY - gMapOffsetY) * SNAP_SPEED;

    // Check if we are close enough to stop snapping
    if (std::abs(gTargetOffsetX - gMapOffsetX) < 1.0f &&
        std::abs(gTargetOffsetY - gMapOffsetY) < 1.0f) {

        gMapOffsetX = gTargetOffsetX;
        gMapOffsetY = gTargetOffsetY;
        gIsSnapping = false;
    }
}

// --- Event Handling ---
/**
 * Centralized function to poll all events and set the quit flag if needed.
 */
void handleEvents(bool& quit_flag) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
        case SDL_EVENT_QUIT:
            quit_flag = true; // Set the main loop's quit flag
            break;
        case SDL_EVENT_WINDOW_RESIZED:
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
            // Window size is handled locally in relevant functions (drawTile, snapToNearestTile)
            break;

        case SDL_EVENT_MOUSE_WHEEL: {
            gIsSnapping = false; // Stop snapping on zoom

            float zoomChange = (event.wheel.y > 0) ? 1.1f : 1.0f / 1.1f;
            float oldZoom = gZoomLevel;
            gZoomLevel = std::max(0.5f, std::min(3.0f, gZoomLevel * zoomChange));

            if (oldZoom != gZoomLevel) {
                // Get mouse position for zoom centering
                float mouseX, mouseY;
                SDL_GetMouseState(&mouseX, &mouseY);

                // Calculate the cartesian point that should remain stationary
                SDL_FPoint stationaryCart = screenToCartesian(mouseX, mouseY);

                // Update tile sizes for new zoom level
                updateTileSizes();

                // Calculate where that stationary cartesian point should now be drawn
                float newTargetX = (stationaryCart.x - stationaryCart.y) * gTileWidth / 2.0f;
                float newTargetY = (stationaryCart.x + stationaryCart.y) * gTileHeight / 2.0f;

                // Adjust offset to keep mouse position stable (zoom centered on cursor)
                gMapOffsetX = mouseX - newTargetX;
                gMapOffsetY = mouseY - newTargetY;
            }
            break;
        }

        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            if (event.button.button == SDL_BUTTON_LEFT) {
                gIsDragging = true;
                gIsSnapping = false; // Stop snapping when drag starts
                gLastMouseX = event.button.x;
                gLastMouseY = event.button.y;
            }
            break;

        case SDL_EVENT_MOUSE_BUTTON_UP:
            if (event.button.button == SDL_BUTTON_LEFT) {
                if (gIsDragging) {
                    gIsDragging = false;
                    // Trigger snap only after a drag is complete
                    snapToNearestTile();
                }
            }
            break;

        case SDL_EVENT_MOUSE_MOTION:
            if (gIsDragging) {
                float currentX = event.motion.x;
                float currentY = event.motion.y;

                float deltaX = currentX - gLastMouseX;
                float deltaY = currentY - gLastMouseY;

                gMapOffsetX += deltaX;
                gMapOffsetY += deltaY;

                gLastMouseX = currentX;
                gLastMouseY = currentY;
            }
            break;
        }
    }
}

// --- Main Loop ---

int main(int argc, char* args[]) {
    if (!init()) {
        std::cerr << "Failed to initialize!" << std::endl;
        return 1;
    }

    generateMap();

    // Initial centering of the map
    updateTileSizes();
    float initialCartX = (MAP_SIZE - 1) / 2.0f;
    float initialCartY = (MAP_SIZE - 1) / 2.0f;
    float targetOriginX = (initialCartX - initialCartY) * gTileWidth / 2.0f;
    float targetOriginY = (initialCartX + initialCartY) * gTileHeight / 2.0f;

    // Center the map view initially
    gMapOffsetX = INITIAL_SCREEN_WIDTH / 2.0f - targetOriginX;
    gMapOffsetY = INITIAL_SCREEN_HEIGHT / 2.0f - targetOriginY;


    bool quit = false;
    Uint64 lastTime = SDL_GetTicks();

    while (!quit) {
        Uint64 currentTime = SDL_GetTicks();
        // float deltaTime = (currentTime - lastTime) / 1000.0f; // DeltaTime currently unused
        lastTime = currentTime;

        // --- Handle Events ---
        handleEvents(quit); // Centralized event polling

        // --- Update ---
        updateCamera(); // Smoothly move camera if snapping

        // --- Render ---
        draw();
    }

    close();
    return 0;
}
