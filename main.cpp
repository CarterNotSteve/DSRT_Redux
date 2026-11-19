#include <iostream>
#include <filesystem>
#include <fstream>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <string>
#include <SDL3/SDL.h> // Lib SDL content
#include <SDL3/SDL_video.h>
#include <SDL3/SDL_render.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_time.h>
#include <SDL3/SDL_timer.h>
#include <SDL3/SDL_filesystem.h>

using namespace std;

class coordinateSystem {
    private:
        SDL_Rect basis;
        double aspectRatio;
        bool isInit = false;

    public:
        typedef union {
            uint32_t __attribute__((vector_size(8))) absolute_simd;
            struct {
                uint32_t x;
                uint32_t y;
            };
        } absolute;
        typedef union {
            float __attribute__((vector_size(8))) normal_simd;
            struct {
                float x;
                float y;
            };
        } normalized;

        typedef uint32_t __attribute__((vector_size(8))) u32array;
        typedef float __attribute__((vector_size(8))) floatArray;

        const absolute coordinate_failure_abs_type{UINT32_MAX, UINT32_MAX};
        const normalized coordinate_failure_norm_type{(float)UINT32_MAX, (float)UINT32_MAX};

        // uint32_t* getCoordAsArray(absolute from) {return (uint32_t[2]){from.absolute_simd[0], from.absolute_simd[1]};}
        // float* getCoordAsArray(normalized from) {return (float[2]){from.x, from.y};}
        u32array getArray(absolute from) {
            return from.absolute_simd;
        }
        floatArray getArray(normalized from) {
            return from.normal_simd;
        }

        coordinateSystem(SDL_Rect* bounding) { // Init coordinate system with bounding box as model
            if (!(bounding->w) || !(bounding->h)) {isInit=false;} // break if zero size
            else {
                basis.w=bounding->w;basis.h=bounding->h;
                basis.x=bounding->x;basis.y=bounding->y;
                aspectRatio = (double)basis.w/(double)basis.h; // e.g. 16/9 = 1.778
                isInit = true;
            }
        }
        coordinateSystem() { // Uninitialized coordinate system, still able to use vectors.
            isInit = false;
        }
        void initializeCoordinateBasis(SDL_Rect* newBasis) {
            basis.x=newBasis->x; basis.y=newBasis->y; // copy properties :/
            basis.w=newBasis->w; basis.h=newBasis->h;
            isInit=true;
        }
        absolute getHome() {
            if (!isInit) {return coordinate_failure_abs_type;}
            return absolute{(uint32_t)basis.x, (uint32_t)basis.y};
        }
        absolute getLocal(double normalX, double normalY) {
            if (!isInit) {return coordinate_failure_abs_type;}
            return absolute{(uint32_t)(normalX*basis.w), (uint32_t)(normalY*basis.h)};
        }
        normalized normalize(uint32_t absX, uint32_t absY) {
            if (!isInit) {return coordinate_failure_norm_type;}
            return normalized{((float)absX/basis.w), ((float)absY/basis.h)};
        }
        const uint32_t& width = basis.w;
        const uint32_t& height = basis.h;
        void operator=(coordinateSystem alius) {
            basis.w = alius.width; basis.h = alius.height;
            absolute ergiZero = alius.getHome();
            basis.x = ergiZero.x; basis.y = ergiZero.y;
            if (!isInit) {
                isInit=true;
            }
        }
};

class map : public coordinateSystem {
    private:
        uint32_t width, height;
        SDL_Texture* mapData;
        SDL_Surface* readOnlyMap;
        SDL_Renderer** renderRef;
        bool mapInit=false;

        bool parseByteByIndex(uint8_t byte, uint8_t bitIndex) {
            return ((byte & (0x80 >> bitIndex))!=0); // Lazy parse; Bitmask nonzero?
        }

    public:
        SDL_Texture*& publicMapData = mapData; // Goddamn! A reference to a pointer. Holy shit.
        coordinateSystem::u32array getArrayForm(coordinateSystem::absolute from) {
            coordinateSystem::u32array tmp{from.x,from.y};
            return tmp;
        }
        // Load Map image from file. Returns false on failure, returns true if everything went right.
        bool loadFromFile(char* filename) {
            if (mapInit) {
                SDL_UnlockTexture(mapData);
            }
            printf("\n-----[DIAGNOSIS][DUMP]-----\n\n");
            mapInit=false; // De-initialize in case first load works and second doesn't.
            if (!std::filesystem::exists(std::filesystem::path(filename))) {
                printf("Did not find file named %s in working directory.\n",filename);
                return false;
            } else {
                printf("Found file %s in current directory. \n |-> %s\n",filename,std::filesystem::current_path().c_str());
            }
            std::ifstream ifMapBin = (std::filesystem::path(filename)); // Input file map binary
            uint8_t filetype=0;
            // ----- [Handle map.pbm/PBM filetype] ----- // -- PRE-INIT -- //
            absolute mapSize{0,0};
            char txInfo[14]; // P 4 0x00 [width] 0x00 [height] 0x00 [tx bits] // W&H here allow <=4 bytes (<=9999 cells w&h)
            for (uint8_t i=0; i<14 && !ifMapBin.eof(); i++) {
                ifMapBin.get(txInfo[i]);
                // printf("iterator load: %d/%ld;\n", i, sizeof(txInfo));
            }
            /* printf("[DIAGNOSIS] map info loaded. Found content:\n |-> [");
            for (uint8_t i=0; i< sizeof(txInfo); printf("POS %d: %02X| ",i,txInfo[i++]));
            printf(" ]\n");*/
            if (txInfo[0] == 'P') {
                if (txInfo[1] == '4') {
                    // Read in width info and ensures P4 magic bytes (eg. PBM Filetype, "New" PBM).
                    filetype=4;
                    uint8_t esiIterator=3; // temp short int iter -- I know. Technically it's a char.
                    //printf("Getting width from positions %d (0x%02X)", esiIterator, txInfo[esiIterator]);
                    while (txInfo[esiIterator] >= 0x30 && txInfo[esiIterator] < 0x3A) { // 0x30='0', 0x3A='@'; 0x39='9'
                        mapSize.x *= 10; mapSize.x += (txInfo[esiIterator++] - 0x30); // In domain [0x30, 0x3A) or [0x30,0x39], H-0x30=dec
                        //printf(", %d (%02X)",esiIterator, txInfo[esiIterator-1]);
                    }
                    printf("\nGot width of %d \n", mapSize.x);
                    esiIterator++; // Breaks when finds whitespace; sets iter to first character of height
                    //printf("\nGetting height at positions %d, ", esiIterator);
                    while (txInfo[esiIterator] >= 0x30 && txInfo[esiIterator] < 0x3A) { // 0x30='0', 0x3A='@'; 0x39='9'
                        mapSize.y *= 10; mapSize.y += (txInfo[esiIterator++] - 0x30); // Turns ascii into integer
                        //printf(", %d (0x%02X)",esiIterator, txInfo[esiIterator-1]);
                    }
                    printf("\nGot Height of %d\n",mapSize.y);
                    u32array mapsizearray = getArrayForm(mapSize);
                    printf("Map Size found: %d x %d \n", mapsizearray[0], mapsizearray[1]);
                    if ((mapsizearray[0]==0)||(mapsizearray[1]==0)) {printf("malformed? found: |%s|\n",txInfo);return false;} // failure; invalid. Malformed. (!0==1, !(n>0)==0)
                    // Create map texture. 4b CD gives me 16^4 colours which leaves room for colour parsing in future.
                    mapData = SDL_CreateTexture(*renderRef, SDL_PIXELFORMAT_RGBA4444, SDL_TEXTUREACCESS_TARGET, mapsizearray[0], mapsizearray[1]);
                    ifMapBin.seekg(ifMapBin.beg + ++esiIterator); // Set file pointer to the first byte of data
                    printf("Moving file pointer to position @ %d\n", esiIterator);
                    // ++ is added here as loops end on one whitespace byte before data.
                }
                // Optional/TODO:: Add P1 PBM support here.
                else {printf("Text accumulated: %s\n",txInfo) ; ifMapBin.close();return false;} // Return failure on wrong filetype.
            }
            
            char* fileMemory = (char*)calloc((mapSize.x*mapSize.y)+mapSize.y+1, sizeof(char)); // +1 for \n

            printf("[DIAGNOSIS] Beginning to load file content in full... \n");
            for (uint32_t filememIndex=0; filememIndex < (mapSize.x*mapSize.y)/8;filememIndex++) { // load bytes
                // printf("Getting character at [%d]\n", filememIndex);
                ifMapBin.get(fileMemory[filememIndex]);
            }
            printf("File fully loaded to memory. Releasing File pointer.\n");
            ifMapBin.close(); // release file. 
            u32array mapsizearray = getArrayForm(mapSize);
            SDL_SetRenderTarget(*renderRef, mapData);
            auto getNearestMultiple = [](uint32_t limit, uint32_t mult){uint32_t max=0; for (uint32_t test=0;test*mult<=limit;test++,max=test); return max*mult;};
            printf("Checking Filetype to parse: Filetype=%d;\n",filetype);
            // -----[PARSE]----- // -- Decided to load into memory after all. Memory is faster than disk.
            if (filetype==4) { // Not redundant; Can add type 1/old pbm, or pgm/ppm in the future.
                const uint32_t eLineLength = (mapSize.x%8==0)?mapSize.x:getNearestMultiple(mapSize.x+8,8); // Line length in Bits including DCB
                // That means: If x is a multiple of 8, length is just width. If not, It's the ^ nearest multiple of eight greather than width.
                printf("Beginning to parse file to texture in memory.\n");
                SDL_FRect pixelPoint; pixelPoint.h = 1; pixelPoint.w = 1; pixelPoint.x=0; pixelPoint.y=0;
                // printf("Image Size: %d | %d\n",mapSize.x, mapSize.y);
                SDL_SetRenderDrawColor(*renderRef, 0,0,0,255);
                for (uint32_t parsedBit=0; parsedBit < eLineLength*mapSize.y; parsedBit++) { // Bit width times height
                    if (parsedBit % eLineLength < mapSize.x) { // If not checking a non-map bit
                        /// printf("Parsing bit [%d]/[%d]: ", parsedBit,eLineLength*mapSize.y);
                        unsigned short bit = fileMemory[parsedBit/8] & (0x80 >> parsedBit%8); // bitmask; byte AND 0b1000 0000 -> 01000000 -> 00100000...
                        // printf("[%s]\n", bit>0?"Yes":"No");
                        if (bit) {
                            SDL_RenderFillRect(*renderRef, &pixelPoint);
                            printf("1");
                        } else {printf("0");}
                        //int32_t firsty = pixelPoint.y;
                        if (pixelPoint.x + 1 >= mapSize.x) {
                            pixelPoint.x=0;
                            pixelPoint.y++;
                        } else {
                            pixelPoint.x++;
                        }
                        
                        //int32_t secondy = pixelPoint.y;
                        //if (secondy - firsty == 1) {printf("\n");}
                    }
                    if ((parsedBit+1)%eLineLength==0) {printf("\n");}
                }
                printf("Memory loaded into texture structure in memory. Freeing.\n");
            }

            free(fileMemory); // Release memory access; Map drawn.
            width=mapSize.x; height=mapSize.y;
            // -----[SUCCESS]----- //
            SDL_SetRenderTarget(*renderRef, NULL);
            SDL_LockTextureToSurface(mapData, NULL, &readOnlyMap);
            // printf("Map locked to surface. Returning True.\n");
            
            mapInit = true;
            return true;
        }
        const uint32_t& pWidth = width; const uint32_t pHeight = height;

        int8_t pollCoordinate(int32_t testX, int32_t testY) { // On failure, returns -1. On success, +1 or 0 for "is there" or "not".
            if (!mapInit) {return -1;}
            else if (testX < 0 || testX > width || testY < 0 || testY > height) {return -1;}
            uint8_t color[4]; uint8_t target_color[4]={0,0,0,255};
            
            SDL_ReadSurfacePixel(readOnlyMap, testX, testY, &(color[0]), &(color[1]), &(color[2]),&(color[3]));
            return (color == target_color)?1:0;
        }

        coordinateSystem::absolute mapFloatAbs2IntAbs(float x, float y, float origin[2]) {
                    coordinateSystem::absolute mappedResult{0,0};
                    mappedResult.x = (origin[0] < x)?floorf(x):ceilf(x);
                    mappedResult.y = (origin[1] < y)?floorf(y):ceilf(y);
                    // round to nearest absolute; steps work bc if it isn't sufficiently different, same.
                    return mappedResult;
                }

        void whichRenderer(SDL_Renderer** suppliedRender) {
            renderRef = suppliedRender;
        }

        ~map() {
            SDL_DestroyTexture(mapData);
        }
};

class player {
    private:
        map imageMap;
        // convenient distance function
        double convDist(double Xa, double Ya, double Xb, double Yb) {return sqrt(pow(Xb-Xa,2)+pow(Yb-Ya,2));}
        class normVec {
            private:
                float __attribute__((vector_size(8))) components_simd;
                map* mapInfo;
                float stepDivisor = 100;
                player* parent;
                const float deg2rad=3.14159265358/180;
                const float rad2deg=1/deg2rad;
                
            public:
                const float vectorErrorCode=3141;
                const float& pStepDiv = stepDivisor;
                void setParent(player* nparent) {
                    parent=nparent;
                }
                void set(float compX, float compY) {
                    components_simd[0]=compX; components_simd[1]=compY;
                    float efMag = sqrtf(compX*compX+compY*compY);
                    components_simd /= efMag;
                }
                void set(float deg) {
                    components_simd[0]=cosf(deg*deg2rad); components_simd[1]=sinf32(deg*deg2rad);
                }
                void setMapReference(map* mapRef) {
                    mapInfo = mapRef;
                }
                void setGridDivisions(float nStepDiv) {stepDivisor=nStepDiv;}
                normVec(float compX, float compY) {
                    set(compX, compY);
                }
                normVec(float deg) {
                    set(deg);
                }
                normVec() {
                    components_simd[0]=1;components_simd[1]=1; // hyp=1.414/sqrt(2); invalid -- should be 1; normalized vector.
                }
                float getAngle() { // WARNING: Returns angle in RADIANS
                    if (!(parent->convDist(0,0,components_simd[0],components_simd[1])==1)) {return vectorErrorCode;} // normalized vec
                    return atan(components_simd[1]/components_simd[0]); // tan=sin/cos=dy/dx=rise/run=slope
                }
                float getOffsetAngle(float offsetdeg) {
                    float finalOffset=(getAngle() + (offsetdeg*deg2rad)); // IN RADIANS
                    if (finalOffset==vectorErrorCode+(offsetdeg*deg2rad)) {return vectorErrorCode;}
                    // manual sinusoidal clamp; 360 < 361 -> result=361-(360/1)
                    if (abs((finalOffset*rad2deg)/360.0f) >= 1) {finalOffset-=deg2rad*(360.0f*floorf(finalOffset/360.0f));}
                    else if (finalOffset*rad2deg<0) {finalOffset+=360.0f*deg2rad;}
                    return finalOffset; // RETURNS RADIANS
                }
                void ipRotate(float dTheta) {
                    float displaced=getOffsetAngle(dTheta)*rad2deg;
                    if (displaced!=367 && displaced<360) {
                        set(getOffsetAngle(dTheta));
                    }
                }
                double getRaycastDistance(float begin[2], float degOffset) {
                    double distance=0;
                    float stepSize = mapInfo->pWidth / stepDivisor;
                    for (uint32_t distIterator=1; mapInfo->pollCoordinate(begin[0]+distIterator*stepSize*cos(getOffsetAngle(degOffset)),
                        begin[1]+distIterator*stepSize*sin(getOffsetAngle(degOffset)))==0; distIterator++,distance+=stepSize);
                    return distance;
                }
                
                coordinateSystem::absolute follow(float begin[2], int32_t steps) {
                    if (components_simd[0] == 1 && components_simd[1] == 1) {return mapInfo->coordinate_failure_abs_type;}
                    // return failure if vector is uninitialized
                    float path[2] = {begin[0], begin[1]};
                    float stepSize = mapInfo->pWidth / stepDivisor;
                    path[0]+=(steps*stepSize*components_simd[0]); path[1]+=(steps*stepSize*components_simd[1]);
                    return mapInfo->mapFloatAbs2IntAbs(path[0],path[1], begin);
                }
        } vLooking;
        float pos[2];
        double FOV=75; // 75deg FOV

        double* frameDist = (double*)calloc(2000,sizeof(double)); // frame memory in RAM

    public:
        // Returns an absolute XY coordinate; the nearest position on the map that is not wall.
        coordinateSystem::absolute getNearestValidPosition(coordinateSystem::normalized requested) {
            if (imageMap.pollCoordinate(requested.x, requested.y)==0) {return imageMap.getLocal(requested.x,requested.y);} // bypass if valid
            coordinateSystem::absolute searchOrigin = imageMap.getLocal(requested.x,requested.y);
            int32_t offset[2] = {0,0};
            coordinateSystem::absolute send{0,0};
            uint8_t direction = 0; bool alternator=false; // altr is binary derivative of direction; delta={01,10,01}; 00,01,11,10 SEE?!
            // if(a){direction^=0x01;} if(!a){direction^=0x02;}; direction LE = X00[+/-X?][+/-Y?]
            double nearestDistance = 32768; // placeholder
            for (uint8_t rad=1; rad < 15; rad++) {
                offset[0] = rad;
                for (uint8_t angle=0; angle<(4*rad); angle++) { // Time Complexity = SUM(n=1->15){4n}
                    int8_t polled=(imageMap.pollCoordinate(requested.x + offset[0], requested.y+offset[1]));
                    if (polled==1) {
                        if (nearestDistance > convDist(0,0,offset[0],offset[1])) {
                            nearestDistance = convDist(0,0,offset[0],offset[1]);
                            send.x = searchOrigin.x+offset[0]; send.y=searchOrigin.y+offset[1];
                        }
                    } else if (polled ==-1) {
                        nearestDistance=32767; // Recoverable OOB
                    }
                    // iterator/radius is a !0 whole number (1,2,3);           1 << 0 or 1 << 1;    swap derivative
                    if ((angle/rad)==floor(angle/rad) && angle>0) {direction^=(0x01<<alternator);alternator=!alternator;} 
                    // at 1, diamond of 1, 2, diamond 2, etc.
                    offset[0] += ((direction&2)?1:-1); offset[1] += ((direction&1)?1:-1); // See alternator comment
                    // 00:-X-Y; 01:-X+Y; 11:+X+Y; 10:+X-Y;
                }
                if (nearestDistance != 32768 && nearestDistance != 32767) {break;} // FOUND!
            }
            if (nearestDistance == 32767 || nearestDistance == 32768) {return imageMap.coordinate_failure_abs_type;}
            return send;
        }

        bool cmpArrays(coordinateSystem::floatArray a, coordinateSystem::floatArray b) {return (a[0]==b[0]&&a[1]==b[1]);}
        bool cmpArrays(coordinateSystem::u32array a, coordinateSystem::u32array b) {return (a[0]==b[0]&&a[1]==b[1]);}
        coordinateSystem::u32array getArrayForm(coordinateSystem::absolute from) {
            coordinateSystem::u32array tmp{from.x,from.y};
            return tmp;
        }

        SDL_Texture* getMapReferenceImage() {return imageMap.publicMapData;}


        player(SDL_Rect* basisReference, coordinateSystem::normalized spawnPoint, double fieldOfVisionWidth) {
            // -----[INIT]----- //
            imageMap.initializeCoordinateBasis(basisReference);
            vLooking.setMapReference(&imageMap);
            coordinateSystem::absolute spawn = getNearestValidPosition(spawnPoint);
            if (!cmpArrays((coordinateSystem::u32array){spawn.x,spawn.y},getArrayForm(imageMap.coordinate_failure_abs_type))) {
                pos[0]=spawn.x; pos[1]=spawn.y;   
            }
            FOV=fieldOfVisionWidth;
        }
        ~player() {free(frameDist);}

        double* calculateFrame(int32_t samplesCt) {
            double step = FOV/(double)samplesCt; // degrees per frame divided by number of splits per frame
            for (int32_t castNum=1;castNum<samplesCt;
                frameDist[castNum-1]=vLooking.getRaycastDistance(pos,(float)(castNum -(samplesCt/2))*step), castNum++, printf("%f ",frameDist[castNum-2]));
                printf("\n");
            return frameDist; // :(  Heap. Freed on destructor, thankfully.
        }


        // Automatically draws the image of the scene the player should see. Does not present or update render. Just fillrect.
        // if you want a color, you should set it before calling this. It will just use whatever color is currently set.
        void drawSightFrame(SDL_Renderer* renderer, SDL_Rect* DisplayBounds, int32_t samples) {
            // memcpy(frameDist, calculateFrame(samples), samples);
            calculateFrame(samples); // frameDist is shared between functions within the class
            SDL_FRect pen;
            float step =DisplayBounds->w/samples; int32_t eh=DisplayBounds->h;
            pen.w=(int)floorf(step);
            for (uint32_t line=0; line<samples;line++) {
                pen.h=eh/frameDist[line]; pen.y=(eh-pen.h)/2; 
                SDL_RenderFillRect(renderer, &pen);
                pen.x+= pen.w;
            }
        }

        void drawSightLazy(SDL_Renderer* renderer, SDL_Rect* displayBounds, int32_t samples) {
            SDL_FRect pen; pen.x=0;
            float step=displayBounds->w/samples; int32_t eh=displayBounds->h;
            pen.w =(int)floorf(step);
            for (uint32_t line=0; line < samples; line++) {
                pen.h=eh/pow(frameDist[line],2); pen.y=(eh-pen.h)/2;
                SDL_RenderFillRect(renderer, &pen);
                pen.x += pen.w;
            }
        }

        bool putImage(char* fname) {
            bool success = imageMap.loadFromFile(fname);
            printf("[INIT] Image Loading %s from Player!\n", success?"Succeeded":"Failed");
            return success;
        }

        void rotateCamera(double degrees) {
            vLooking.ipRotate(degrees);
        }

        void goForward(float steps) { // changed from uint8_t to float -- Enables usage of deltaTime later on.
            pos[0] += cos(vLooking.getAngle())*vLooking.pStepDiv*(float)steps;
            pos[1] += sin (vLooking.getAngle())*vLooking.pStepDiv*(float)steps;
        }
        void goForward() {goForward(1);} // alias

        void supplyRender(SDL_Renderer** supply) {
            imageMap.whichRenderer(supply);
        }

};

int main(int argc, char** argv) {
    // RUN ONCE -- INIT
    SDL_Init(SDL_INIT_VIDEO);
    SDL_DisplayID PRIMARY_DISPLAY = SDL_GetPrimaryDisplay();
    SDL_Rect SCREENSIZE; SDL_GetDisplayBounds(PRIMARY_DISPLAY, &SCREENSIZE);

    coordinateSystem coordSys(&SCREENSIZE);
    //printf("[INIT] Width: %d | Height: %u \n",coordSys.width,coordSys.height);
    //printf("[INIT] Rect reports: %d | %i \n", SCREENSIZE.w, SCREENSIZE.h);
    
    SDL_Window* window = SDL_CreateWindow("DSRT Redux", SCREENSIZE.w, SCREENSIZE.h, 0);
    SDL_SetWindowFullscreen(window, true); // Borderless FS
    SDL_SetWindowBordered(window, false);
    SDL_Renderer* render = SDL_CreateRenderer(window, NULL);
    bool RUNNING_GAME = true;

    int32_t rendersizeW=0;int32_t rendersizeH=0;
    SDL_GetCurrentRenderOutputSize(render, &rendersizeW, &rendersizeH);
    //printf("[INIT][DEBUG] Render size reports: %d | %d ;\n", rendersizeW, rendersizeH);

    SDL_Event eQueue;

    bool didVisionChange = true;

    player agent(&SCREENSIZE,coordinateSystem::normalized{0.5,0.5},75);
    agent.supplyRender(&render);
    char* filename = (argc>1)?argv[1]:(char*)"map.pbm";
    if (!agent.putImage(filename)) {printf("Failed to load image @ %s\n",filename); RUNNING_GAME=false;} // map load failure; skip, and cleanup to exit.

    const bool* keysAll = SDL_GetKeyboardState(NULL);
    

    uint64_t lastFrameTime=0;
    uint64_t nowFrameTime=0;
    auto getDeltaTime = [lastFrameTime,nowFrameTime](){return nowFrameTime-lastFrameTime;};
    uint64_t deltaTime=0;


    // RUN LOOP -- RUNS CONTINUOUS
    while (RUNNING_GAME) {
        deltaTime = getDeltaTime();
        while (SDL_PollEvent(&eQueue)) {
            if (eQueue.type==SDL_EVENT_QUIT) { // Might be a little important.
                RUNNING_GAME=false;
            }
        }
        nowFrameTime = SDL_GetTicks();
        if (keysAll[SDL_SCANCODE_LEFT] && !keysAll[SDL_SCANCODE_RIGHT]) {
            agent.rotateCamera(-0.8);
            didVisionChange=true;
        } else if (keysAll[SDL_SCANCODE_RIGHT] && !keysAll[SDL_SCANCODE_LEFT]) {
            agent.rotateCamera(0.8);
            didVisionChange=true;
        }

        if (keysAll[SDL_SCANCODE_W]) {
            agent.goForward(deltaTime/1000); // dT in ms -> ms/1000=s so velocity \propto \Delta t. Yes I used LaTeX in a comment. Fight me.
            didVisionChange=true;
        }
        if (keysAll[SDL_SCANCODE_ESCAPE]) {
            RUNNING_GAME=false; // Just to make sure, ensure eject handle.
        }
        
        SDL_SetRenderDrawColor(render, 255,255,255,255);
        SDL_RenderClear(render);
        SDL_SetRenderDrawColor(render, 100,100,50,255);
        if (didVisionChange) {
            agent.calculateFrame(128); // If something changed, recalc.
            didVisionChange = false; // redraw lazy next if nothing changes.
        }
        agent.drawSightLazy(render, &SCREENSIZE, 128); // Don't recompute if nothing changed. Foveated, basically.
        SDL_FRect mapRender; mapRender.h =64; mapRender.w = 64; mapRender.x = 20; mapRender.y=20;
        SDL_RenderTexture(render,agent.getMapReferenceImage(),NULL, &mapRender);
        //SDL_FRect testDraw; testDraw.w = 32; testDraw.h = 32; testDraw.x = 300; testDraw.y = 300;
        // SDL_RenderTexture(render, agent.getMapReferenceImage(), NULL, &testDraw);
        SDL_RenderPresent(render);

        lastFrameTime=nowFrameTime;
    }

    // CLEANUP -- FREE MEMORY, RELEASE ASSETS
    SDL_DestroyRenderer(render);
    SDL_DestroyWindow(window);
    // agent.~player();

    return 0;
}