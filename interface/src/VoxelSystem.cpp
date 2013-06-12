//
//  Cube.cpp
//  interface
//
//  Created by Philip on 12/31/12.
//  Copyright (c) 2012 High Fidelity, Inc. All rights reserved.
//
#ifdef _WIN32
#define _timeval_
#define _USE_MATH_DEFINES
#endif
#include <cstring>
#include <cmath>
#include <iostream> // to load voxels from file
#include <fstream> // to load voxels from file
#include <glm/gtc/random.hpp>
#include <SharedUtil.h>
#include <PacketHeaders.h>
#include <PerfStat.h>
#include <OctalCode.h>
#include <pthread.h>
#include "Application.h"
#include "Log.h"
#include "VoxelConstants.h"
#include "CoverageMap.h"
#include "InterfaceConfig.h"
#include "renderer/ProgramObject.h"

#include "VoxelSystem.h"

float identityVertices[] = { 0,0,0, 1,0,0, 1,1,0, 0,1,0, 0,0,1, 1,0,1, 1,1,1, 0,1,1,
                             0,0,0, 1,0,0, 1,1,0, 0,1,0, 0,0,1, 1,0,1, 1,1,1, 0,1,1,
                             0,0,0, 1,0,0, 1,1,0, 0,1,0, 0,0,1, 1,0,1, 1,1,1, 0,1,1 };

GLfloat identityNormals[] = { 0,0,-1, 0,0,-1, 0,0,-1, 0,0,-1,
                              0,0,+1, 0,0,+1, 0,0,+1, 0,0,+1,
                              0,-1,0, 0,-1,0, 0,+1,0, 0,+1,0,
                              0,-1,0, 0,-1,0, 0,+1,0, 0,+1,0,
                              -1,0,0, +1,0,0, +1,0,0, -1,0,0,
                              -1,0,0, +1,0,0, +1,0,0, -1,0,0 };

GLubyte identityIndices[] = { 0,2,1,    0,3,2,    // Z-
                              8,9,13,   8,13,12,  // Y-
                              16,23,19, 16,20,23, // X-
                              17,18,22, 17,22,21, // X+
                              10,11,15, 10,15,14, // Y+
                              4,5,6,    4,6,7 };  // Z+

VoxelSystem::VoxelSystem(float treeScale, int maxVoxels) :
        AgentData(NULL), _treeScale(treeScale), _maxVoxels(maxVoxels) {
    _voxelsInReadArrays = _voxelsInWriteArrays = _voxelsUpdated = 0;
    _writeRenderFullVBO = true;
    _readRenderFullVBO = true;
    _tree = new VoxelTree();
    pthread_mutex_init(&_bufferWriteLock, NULL);
    pthread_mutex_init(&_treeLock, NULL);
}

VoxelSystem::~VoxelSystem() {
    delete[] _readVerticesArray;
    delete[] _writeVerticesArray;
    delete[] _readColorsArray;
    delete[] _writeColorsArray;
    delete[] _writeVoxelDirtyArray;
    delete[] _readVoxelDirtyArray;
    delete _tree;
    pthread_mutex_destroy(&_bufferWriteLock);
    pthread_mutex_destroy(&_treeLock);
}

void VoxelSystem::loadVoxelsFile(const char* fileName, bool wantColorRandomizer) {
    _tree->loadVoxelsFile(fileName, wantColorRandomizer);
    setupNewVoxelsForDrawing();
}

void VoxelSystem::writeToSVOFile(const char* filename, VoxelNode* node) const {
    _tree->writeToSVOFile(filename, node);
}

bool VoxelSystem::readFromSVOFile(const char* filename) {
    bool result = _tree->readFromSVOFile(filename);
    if (result) {
        setupNewVoxelsForDrawing();
    }
    return result;
}

long int VoxelSystem::getVoxelsCreated() {
    return _tree->voxelsCreated;
}

float VoxelSystem::getVoxelsCreatedPerSecondAverage() {
    return (1 / _tree->voxelsCreatedStats.getEventDeltaAverage());
}

long int VoxelSystem::getVoxelsColored() {
    return _tree->voxelsColored;
}

float VoxelSystem::getVoxelsColoredPerSecondAverage() {
    return (1 / _tree->voxelsColoredStats.getEventDeltaAverage());
}

long int VoxelSystem::getVoxelsBytesRead() {
    return _tree->voxelsBytesRead;
}

float VoxelSystem::getVoxelsBytesReadPerSecondAverage() {
    return _tree->voxelsBytesReadStats.getAverageSampleValuePerSecond();
}

int VoxelSystem::parseData(unsigned char* sourceBuffer, int numBytes) {

    unsigned char command = *sourceBuffer;
    unsigned char *voxelData = sourceBuffer + 1;

    pthread_mutex_lock(&_treeLock);

    switch(command) {
        case PACKET_HEADER_VOXEL_DATA:
        {
            PerformanceWarning warn(_renderWarningsOn, "readBitstreamToTree()");
            // ask the VoxelTree to read the bitstream into the tree
            _tree->readBitstreamToTree(voxelData, numBytes - 1, WANT_COLOR, WANT_EXISTS_BITS);
        }
        break;
        case PACKET_HEADER_VOXEL_DATA_MONOCHROME:
        {
            PerformanceWarning warn(_renderWarningsOn, "readBitstreamToTree()");
            // ask the VoxelTree to read the MONOCHROME bitstream into the tree
            _tree->readBitstreamToTree(voxelData, numBytes - 1, NO_COLOR, WANT_EXISTS_BITS);
        }
        break;
        case PACKET_HEADER_Z_COMMAND:

            // the Z command is a special command that allows the sender to send high level semantic
            // requests, like erase all, or add sphere scene, different receivers may handle these
            // messages differently
            char* packetData = (char *)sourceBuffer;
            char* command = &packetData[1]; // start of the command
            int commandLength = strlen(command); // commands are null terminated strings
            int totalLength = 1+commandLength+1;

            printLog("got Z message len(%d)= %s\n", numBytes, command);

            while (totalLength <= numBytes) {
                if (0==strcmp(command,(char*)"erase all")) {
                    printLog("got Z message == erase all\n");
                    _tree->eraseAllVoxels();
                    _voxelsInReadArrays = _voxelsInWriteArrays = 0; // better way to do this??
                }
                if (0==strcmp(command,(char*)"add scene")) {
                    printLog("got Z message == add scene - NOT SUPPORTED ON INTERFACE\n");
                }
                totalLength += commandLength+1;
            }
        break;
    }

    setupNewVoxelsForDrawing();
    
    pthread_mutex_unlock(&_treeLock);
    
    return numBytes;
}

void VoxelSystem::setupNewVoxelsForDrawing() {
    PerformanceWarning warn(_renderWarningsOn, "setupNewVoxelsForDrawing()"); // would like to include _voxelsInArrays, _voxelsUpdated
    double start = usecTimestampNow();
    double sinceLastTime = (start - _setupNewVoxelsForDrawingLastFinished) / 1000.0;

    bool iAmDebugging = false;  // if you're debugging set this to true, so you won't get skipped for slow debugging
    if (!iAmDebugging && sinceLastTime <= std::max(_setupNewVoxelsForDrawingLastElapsed, SIXTY_FPS_IN_MILLISECONDS)) {
        return; // bail early, it hasn't been long enough since the last time we ran
    }

    double sinceLastViewCulling = (start - _lastViewCulling) / 1000.0;
    // If the view frustum is no longer changing, but has changed, since last time, then remove nodes that are out of view
    if ((sinceLastViewCulling >= std::max(_lastViewCullingElapsed, VIEW_CULLING_RATE_IN_MILLISECONDS)) 
            && !isViewChanging() && hasViewChanged()) {
        _lastViewCulling = start;

        // When we call removeOutOfView() voxels, we don't actually remove the voxels from the VBOs, but we do remove
        // them from tree, this makes our tree caclulations faster, but doesn't require us to fully rebuild the VBOs (which
        // can be expensive).
        removeOutOfView();
        
        // Once we call cleanupRemovedVoxels() we do need to rebuild our VBOs (if anything was actually removed). So,
        // we should consider putting this someplace else... as this might be able to occur less frequently, and save us on
        // VBO reubuilding. Possibly we should do this only if our actual VBO usage crosses some lower boundary.
        cleanupRemovedVoxels();

        double endViewCulling = usecTimestampNow();
        _lastViewCullingElapsed = (endViewCulling - start) / 1000.0;
    }    
    
    bool didWriteFullVBO = _writeRenderFullVBO;
    if (_tree->isDirty()) {
        static char buffer[64] = { 0 };
        if (_renderWarningsOn) { 
            sprintf(buffer, "newTreeToArrays() _writeRenderFullVBO=%s", debug::valueOf(_writeRenderFullVBO)); 
        };
        PerformanceWarning warn(_renderWarningsOn, buffer);
        _callsToTreesToArrays++;
        if (_writeRenderFullVBO) {
            _voxelsInWriteArrays = 0; // reset our VBO
        }
        _voxelsUpdated = newTreeToArrays(_tree->rootNode);
        _tree->clearDirtyBit(); // after we pull the trees into the array, we can consider the tree clean
        
        // since we called treeToArrays, we can assume that our VBO is in sync, and so partial updates to the VBOs are
        // ok again, until/unless we call removeOutOfView() 
        _writeRenderFullVBO = false; 
    } else {
        _voxelsUpdated = 0;
    }
    
    // lock on the buffer write lock so we can't modify the data when the GPU is reading it
    pthread_mutex_lock(&_bufferWriteLock);
    
    if (_voxelsUpdated) {
        _voxelsDirty=true;
    }

    // copy the newly written data to the arrays designated for reading, only does something if _voxelsDirty && _voxelsUpdated
    copyWrittenDataToReadArrays(didWriteFullVBO);

    pthread_mutex_unlock(&_bufferWriteLock);

    double end = usecTimestampNow();
    double elapsedmsec = (end - start) / 1000.0;
    _setupNewVoxelsForDrawingLastFinished = end;
    _setupNewVoxelsForDrawingLastElapsed = elapsedmsec;
}

void VoxelSystem::cleanupRemovedVoxels() {
    PerformanceWarning warn(_renderWarningsOn, "cleanupRemovedVoxels()");
    if (!_removedVoxels.isEmpty()) {
        while (!_removedVoxels.isEmpty()) {
            delete _removedVoxels.extract();
        }
        _writeRenderFullVBO = true; // if we remove voxels, we must update our full VBOs
    }
}

void VoxelSystem::copyWrittenDataToReadArraysFullVBOs() {
    copyWrittenDataSegmentToReadArrays(0, _voxelsInWriteArrays - 1);

    _voxelsInReadArrays = _voxelsInWriteArrays;
    
    // clear our dirty flags
    memset(_writeVoxelDirtyArray, false, _voxelsInWriteArrays * sizeof(bool));
    
    // let the reader know to get the full array
    _readRenderFullVBO = true;
}

void VoxelSystem::copyWrittenDataToReadArraysPartialVBOs() {
    glBufferIndex segmentStart = 0;
    bool inSegment = false;
    for (glBufferIndex i = 0; i < _voxelsInWriteArrays; i++) {
        bool thisVoxelDirty = _writeVoxelDirtyArray[i];
        _readVoxelDirtyArray[i] |= thisVoxelDirty;
        _writeVoxelDirtyArray[i] = false;
        if (!inSegment) {
            if (thisVoxelDirty) {
                segmentStart = i;
                inSegment = true;
            }
        } else {
            if (!thisVoxelDirty) {
                // If we got here because because this voxel is NOT dirty, so the last dirty voxel was the one before
                // this one and so that's where the "segment" ends
                copyWrittenDataSegmentToReadArrays(segmentStart, i - 1);
                inSegment = false;
            }
        }
    }
    
    // if we got to the end of the array, and we're in an active dirty segment...
    if (inSegment) {
        copyWrittenDataSegmentToReadArrays(segmentStart, _voxelsInWriteArrays - 1);
    }

    // update our length
    _voxelsInReadArrays = _voxelsInWriteArrays;
}

void VoxelSystem::copyWrittenDataSegmentToReadArrays(glBufferIndex segmentStart, glBufferIndex segmentEnd) {
    int segmentLength = (segmentEnd - segmentStart) + 1;

    GLintptr   segmentStartAt   = segmentStart * VERTEX_POINTS_PER_VOXEL * sizeof(GLfloat);
    GLsizeiptr segmentSizeBytes = segmentLength * VERTEX_POINTS_PER_VOXEL * sizeof(GLfloat);
    GLfloat* readVerticesAt     = _readVerticesArray  + (segmentStart * VERTEX_POINTS_PER_VOXEL);
    GLfloat* writeVerticesAt    = _writeVerticesArray + (segmentStart * VERTEX_POINTS_PER_VOXEL);
    memcpy(readVerticesAt, writeVerticesAt, segmentSizeBytes);

    segmentStartAt          = segmentStart * VERTEX_POINTS_PER_VOXEL * sizeof(GLubyte);
    segmentSizeBytes        = segmentLength * VERTEX_POINTS_PER_VOXEL * sizeof(GLubyte);
    GLubyte* readColorsAt   = _readColorsArray   + (segmentStart * VERTEX_POINTS_PER_VOXEL);
    GLubyte* writeColorsAt  = _writeColorsArray  + (segmentStart * VERTEX_POINTS_PER_VOXEL);
    memcpy(readColorsAt, writeColorsAt, segmentSizeBytes);
}

void VoxelSystem::copyWrittenDataToReadArrays(bool fullVBOs) {
    PerformanceWarning warn(_renderWarningsOn, "copyWrittenDataToReadArrays()");
    if (_voxelsDirty && _voxelsUpdated) {
        if (fullVBOs) {
            copyWrittenDataToReadArraysFullVBOs();
        } else {
            copyWrittenDataToReadArraysPartialVBOs();
        }
    }
}

int VoxelSystem::newTreeToArrays(VoxelNode* node) {
    int   voxelsUpdated   = 0;
    bool  shouldRender    = false; // assume we don't need to render it
    // if it's colored, we might need to render it!
    if (node->isColored()) {
        float distanceToNode  = node->distanceToCamera(*Application::getInstance()->getViewFrustum());
        float boundary        = boundaryDistanceForRenderLevel(node->getLevel());
        float childBoundary   = boundaryDistanceForRenderLevel(node->getLevel() + 1);
        bool  inBoundary      = (distanceToNode <= boundary);
        bool  inChildBoundary = (distanceToNode <= childBoundary);
        shouldRender = (node->isLeaf() && inChildBoundary) || (inBoundary && !inChildBoundary);
    }
    node->setShouldRender(shouldRender && !node->isStagedForDeletion());
    // let children figure out their renderness
    if (!node->isLeaf()) {
        for (int i = 0; i < NUMBER_OF_CHILDREN; i++) {
            if (node->getChildAtIndex(i)) {
                voxelsUpdated += newTreeToArrays(node->getChildAtIndex(i));
            }
        }
    }
    if (_writeRenderFullVBO) {
        voxelsUpdated += updateNodeInArraysAsFullVBO(node);
    } else {
        voxelsUpdated += updateNodeInArraysAsPartialVBO(node);
    }
    node->clearDirtyBit(); // clear the dirty bit, do this before we potentially delete things.
    
    // If the node has been asked to be deleted, but we've gotten to here, after updateNodeInArraysXXX()
    // then it means our VBOs are "clean" and our vertices have been removed or not added. So we can now
    // safely remove the node from the tree and actually delete it.
    if (node->isStagedForDeletion()) {
        _tree->deleteVoxelCodeFromTree(node->getOctalCode());
    }
    
    return voxelsUpdated;
}

int VoxelSystem::updateNodeInArraysAsFullVBO(VoxelNode* node) {
    // If we've run out of room, then just bail...
    if (_voxelsInWriteArrays >= _maxVoxels) {
        return 0;
    }
    
    if (node->getShouldRender()) {
        glm::vec3 startVertex = node->getCorner();
        float voxelScale = node->getScale();
        glBufferIndex nodeIndex = _voxelsInWriteArrays;

        // populate the array with points for the 8 vertices
        // and RGB color for each added vertex
        updateNodeInArrays(nodeIndex, startVertex, voxelScale, node->getColor());
        node->setBufferIndex(nodeIndex);
        _writeVoxelDirtyArray[nodeIndex] = true; // just in case we switch to Partial mode
        _voxelsInWriteArrays++; // our know vertices in the arrays
        return 1; // rendered
    } else {
        node->setBufferIndex(GLBUFFER_INDEX_UNKNOWN);
    }
    
    return 0; // not-rendered
}

int VoxelSystem::updateNodeInArraysAsPartialVBO(VoxelNode* node) {
    // If we've run out of room, then just bail...
    if (_voxelsInWriteArrays >= _maxVoxels) {
        return 0;
    }
    
    // Now, if we've changed any attributes (our renderness, our color, etc) then update the Arrays...
    if (node->isDirty()) {
        glm::vec3 startVertex;
        float voxelScale = 0;
        // If we're should render, use our legit location and scale, 
        if (node->getShouldRender()) {
            startVertex = node->getCorner();
            voxelScale = node->getScale();
        } else {
            // if we shouldn't render then set out location to some infinitely distant location, 
            // and our scale as infinitely small
            startVertex[0] = startVertex[1] = startVertex[2] = FLT_MAX;
            voxelScale = 0;
        }

        // If this node has not yet been written to the array, then add it to the end of the array.
        glBufferIndex nodeIndex;
        if (node->isKnownBufferIndex()) {
            nodeIndex = node->getBufferIndex();
        } else {
            nodeIndex = _voxelsInWriteArrays;
            node->setBufferIndex(nodeIndex);
            _voxelsInWriteArrays++;
        }
        _writeVoxelDirtyArray[nodeIndex] = true;

        // populate the array with points for the 8 vertices
        // and RGB color for each added vertex
        updateNodeInArrays(nodeIndex, startVertex, voxelScale, node->getColor());
        
        return 1; // updated!
    }
    return 0; // not-updated
}

void VoxelSystem::updateNodeInArrays(glBufferIndex nodeIndex, const glm::vec3& startVertex,
                                     float voxelScale, const nodeColor& color) {
    for (int j = 0; j < VERTEX_POINTS_PER_VOXEL; j++ ) {
        GLfloat* writeVerticesAt = _writeVerticesArray + (nodeIndex * VERTEX_POINTS_PER_VOXEL);
        GLubyte* writeColorsAt   = _writeColorsArray   + (nodeIndex * VERTEX_POINTS_PER_VOXEL);
        *(writeVerticesAt+j) = startVertex[j % 3] + (identityVertices[j] * voxelScale);
        *(writeColorsAt  +j) = color[j % 3];
    }
}

glm::vec3 VoxelSystem::computeVoxelVertex(const glm::vec3& startVertex, float voxelScale, int index) const {
    const float* identityVertex = identityVertices + index * 3;
    return startVertex + glm::vec3(identityVertex[0], identityVertex[1], identityVertex[2]) * voxelScale;
}

ProgramObject* VoxelSystem::_perlinModulateProgram = 0;
GLuint VoxelSystem::_permutationNormalTextureID = 0;

void VoxelSystem::init() {

    _renderWarningsOn = false;
    _callsToTreesToArrays = 0;
    _setupNewVoxelsForDrawingLastFinished = 0;
    _setupNewVoxelsForDrawingLastElapsed = 0;
    _lastViewCullingElapsed = _lastViewCulling = 0;

    // When we change voxels representations in the arrays, we'll update this
    _voxelsDirty = false;
    _voxelsInWriteArrays = 0;
    _voxelsInReadArrays = 0;
    _unusedArraySpace = 0;

    // we will track individual dirty sections with these arrays of bools
    _writeVoxelDirtyArray = new bool[_maxVoxels];
    memset(_writeVoxelDirtyArray, false, _maxVoxels * sizeof(bool));
    _readVoxelDirtyArray = new bool[_maxVoxels];
    memset(_readVoxelDirtyArray, false, _maxVoxels * sizeof(bool));

    // prep the data structures for incoming voxel data
    _writeVerticesArray = new GLfloat[VERTEX_POINTS_PER_VOXEL * _maxVoxels];
    _readVerticesArray = new GLfloat[VERTEX_POINTS_PER_VOXEL * _maxVoxels];

    _writeColorsArray = new GLubyte[VERTEX_POINTS_PER_VOXEL * _maxVoxels];
    _readColorsArray = new GLubyte[VERTEX_POINTS_PER_VOXEL * _maxVoxels];

    GLuint* indicesArray = new GLuint[INDICES_PER_VOXEL * _maxVoxels];

    // populate the indicesArray
    // this will not change given new voxels, so we can set it all up now
    for (int n = 0; n < _maxVoxels; n++) {
        // fill the indices array
        int voxelIndexOffset = n * INDICES_PER_VOXEL;
        GLuint* currentIndicesPos = indicesArray + voxelIndexOffset;
        int startIndex = (n * VERTICES_PER_VOXEL);

        for (int i = 0; i < INDICES_PER_VOXEL; i++) {
            // add indices for this side of the cube
            currentIndicesPos[i] = startIndex + identityIndices[i];
        }
    }

    GLfloat* normalsArray = new GLfloat[VERTEX_POINTS_PER_VOXEL * _maxVoxels];
    GLfloat* normalsArrayEndPointer = normalsArray;

    // populate the normalsArray
    for (int n = 0; n < _maxVoxels; n++) {
        for (int i = 0; i < VERTEX_POINTS_PER_VOXEL; i++) {
            *(normalsArrayEndPointer++) = identityNormals[i];
        }
    }

    // VBO for the verticesArray
    glGenBuffers(1, &_vboVerticesID);
    glBindBuffer(GL_ARRAY_BUFFER, _vboVerticesID);
    glBufferData(GL_ARRAY_BUFFER, VERTEX_POINTS_PER_VOXEL * sizeof(GLfloat) * _maxVoxels, NULL, GL_DYNAMIC_DRAW);

    // VBO for the normalsArray
    glGenBuffers(1, &_vboNormalsID);
    glBindBuffer(GL_ARRAY_BUFFER, _vboNormalsID);
    glBufferData(GL_ARRAY_BUFFER,
                 VERTEX_POINTS_PER_VOXEL * sizeof(GLfloat) * _maxVoxels,
                 normalsArray, GL_STATIC_DRAW);

    // VBO for colorsArray
    glGenBuffers(1, &_vboColorsID);
    glBindBuffer(GL_ARRAY_BUFFER, _vboColorsID);
    glBufferData(GL_ARRAY_BUFFER, VERTEX_POINTS_PER_VOXEL * sizeof(GLubyte) * _maxVoxels, NULL, GL_DYNAMIC_DRAW);

    // VBO for the indicesArray
    glGenBuffers(1, &_vboIndicesID);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, _vboIndicesID);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 INDICES_PER_VOXEL * sizeof(GLuint) * _maxVoxels,
                 indicesArray, GL_STATIC_DRAW);

    // delete the indices and normals arrays that are no longer needed
    delete[] indicesArray;
    delete[] normalsArray;
    
    // create our simple fragment shader if we're the first system to init
    if (_perlinModulateProgram != 0) {
        return;
    }
    switchToResourcesParentIfRequired();
    _perlinModulateProgram = new ProgramObject();
    _perlinModulateProgram->addShaderFromSourceFile(QGLShader::Vertex, "resources/shaders/perlin_modulate.vert");
    _perlinModulateProgram->addShaderFromSourceFile(QGLShader::Fragment, "resources/shaders/perlin_modulate.frag");
    _perlinModulateProgram->link();
    
    _perlinModulateProgram->setUniformValue("permutationNormalTexture", 0);
    
    // create the permutation/normal texture
    glGenTextures(1, &_permutationNormalTextureID);
    glBindTexture(GL_TEXTURE_2D, _permutationNormalTextureID);
    
    // the first line consists of random permutation offsets
    unsigned char data[256 * 2 * 3];
    for (int i = 0; i < 256 * 3; i++) {
        data[i] = rand() % 256;
    }
    // the next, random unit normals
    for (int i = 256 * 3; i < 256 * 3 * 2; i += 3) {
        glm::vec3 randvec = glm::sphericalRand(1.0f);
        data[i] = ((randvec.x + 1.0f) / 2.0f) * 255.0f;
        data[i + 1] = ((randvec.y + 1.0f) / 2.0f) * 255.0f;
        data[i + 2] = ((randvec.z + 1.0f) / 2.0f) * 255.0f;
    }
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 256, 2, 0, GL_RGB, GL_UNSIGNED_BYTE, data);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    
    glBindTexture(GL_TEXTURE_2D, 0);
}

void VoxelSystem::updateFullVBOs() {
    updateVBOSegment(0, _voxelsInReadArrays);
    
    // consider the _readVoxelDirtyArray[] clean!
    memset(_readVoxelDirtyArray, false, _voxelsInReadArrays * sizeof(bool));
}

void VoxelSystem::updatePartialVBOs() {
    glBufferIndex segmentStart = 0;
    bool inSegment = false;
    for (glBufferIndex i = 0; i < _voxelsInReadArrays; i++) {
        bool thisVoxelDirty = _readVoxelDirtyArray[i];
        if (!inSegment) {
            if (thisVoxelDirty) {
                segmentStart = i;
                inSegment = true;
                _readVoxelDirtyArray[i] = false; // consider us clean!
            }
        } else {
            if (!thisVoxelDirty) {
                // If we got here because because this voxel is NOT dirty, so the last dirty voxel was the one before
                // this one and so that's where the "segment" ends
                updateVBOSegment(segmentStart, i - 1);
                inSegment = false;
            }
            _readVoxelDirtyArray[i] = false; // consider us clean!
        }
    }
    
    // if we got to the end of the array, and we're in an active dirty segment...
    if (inSegment) {    
        updateVBOSegment(segmentStart, _voxelsInReadArrays - 1);
        inSegment = false;
    }
}

void VoxelSystem::updateVBOs() {
    static char buffer[40] = { 0 };
    if (_renderWarningsOn) { 
        sprintf(buffer, "updateVBOs() _readRenderFullVBO=%s", debug::valueOf(_readRenderFullVBO));
    };
    PerformanceWarning warn(_renderWarningsOn, buffer); // would like to include _callsToTreesToArrays
    if (_voxelsDirty) {
        if (_readRenderFullVBO) {
            updateFullVBOs();
        } else {
            updatePartialVBOs();
        }
        _voxelsDirty = false;
        _readRenderFullVBO = false;
    }
    _callsToTreesToArrays = 0; // clear it
}

void VoxelSystem::updateVBOSegment(glBufferIndex segmentStart, glBufferIndex segmentEnd) {
    int segmentLength = (segmentEnd - segmentStart) + 1;
    GLintptr   segmentStartAt   = segmentStart * VERTEX_POINTS_PER_VOXEL * sizeof(GLfloat);
    GLsizeiptr segmentSizeBytes = segmentLength * VERTEX_POINTS_PER_VOXEL * sizeof(GLfloat);
    GLfloat* readVerticesFrom   = _readVerticesArray + (segmentStart * VERTEX_POINTS_PER_VOXEL);
    glBindBuffer(GL_ARRAY_BUFFER, _vboVerticesID);
    glBufferSubData(GL_ARRAY_BUFFER, segmentStartAt, segmentSizeBytes, readVerticesFrom);
    segmentStartAt          = segmentStart * VERTEX_POINTS_PER_VOXEL * sizeof(GLubyte);
    segmentSizeBytes        = segmentLength * VERTEX_POINTS_PER_VOXEL * sizeof(GLubyte);
    GLubyte* readColorsFrom = _readColorsArray   + (segmentStart * VERTEX_POINTS_PER_VOXEL);
    glBindBuffer(GL_ARRAY_BUFFER, _vboColorsID);
    glBufferSubData(GL_ARRAY_BUFFER, segmentStartAt, segmentSizeBytes, readColorsFrom);
}

void VoxelSystem::render(bool texture) {
    PerformanceWarning warn(_renderWarningsOn, "render()");
    
    // get the lock so that the update thread won't change anything
    pthread_mutex_lock(&_bufferWriteLock);
    
    updateVBOs();
    
    // tell OpenGL where to find vertex and color information
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_NORMAL_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);

    glBindBuffer(GL_ARRAY_BUFFER, _vboVerticesID);
    glVertexPointer(3, GL_FLOAT, 0, 0);

    glBindBuffer(GL_ARRAY_BUFFER, _vboNormalsID);
    glNormalPointer(GL_FLOAT, 0, 0);

    glBindBuffer(GL_ARRAY_BUFFER, _vboColorsID);
    glColorPointer(3, GL_UNSIGNED_BYTE, 0, 0);

    applyScaleAndBindProgram(texture);
    
    // for performance, disable blending and enable backface culling
    glDisable(GL_BLEND);
    glEnable(GL_CULL_FACE);

    // draw the number of voxels we have
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, _vboIndicesID);
    glDrawRangeElementsEXT(GL_TRIANGLES, 0, VERTICES_PER_VOXEL * _voxelsInReadArrays - 1,
        36 * _voxelsInReadArrays, GL_UNSIGNED_INT, 0);

    glEnable(GL_BLEND);
    glDisable(GL_CULL_FACE);

    removeScaleAndReleaseProgram(texture);
    
    // deactivate vertex and color arrays after drawing
    glDisableClientState(GL_VERTEX_ARRAY);
    glDisableClientState(GL_NORMAL_ARRAY);
    glDisableClientState(GL_COLOR_ARRAY);

    // bind with 0 to switch back to normal operation
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    
    pthread_mutex_unlock(&_bufferWriteLock);
}

void VoxelSystem::applyScaleAndBindProgram(bool texture) {
    glPushMatrix();
    glScalef(_treeScale, _treeScale, _treeScale);

    if (texture) {
        _perlinModulateProgram->bind();
        glBindTexture(GL_TEXTURE_2D, _permutationNormalTextureID);
    }
}

void VoxelSystem::removeScaleAndReleaseProgram(bool texture) {
    // scale back down to 1 so heads aren't massive
    glPopMatrix();
    
    if (texture) {
        _perlinModulateProgram->release();
        glBindTexture(GL_TEXTURE_2D, 0);
    }
}

int VoxelSystem::_nodeCount = 0;

void VoxelSystem::killLocalVoxels() {
    _tree->eraseAllVoxels();
    _voxelsInWriteArrays = _voxelsInReadArrays = 0; // better way to do this??
    //setupNewVoxelsForDrawing();
}


bool VoxelSystem::randomColorOperation(VoxelNode* node, void* extraData) {
    _nodeCount++;
    if (node->isColored()) {
        nodeColor newColor = { 255, randomColorValue(150), randomColorValue(150), 1 };
        node->setColor(newColor);
    }
    return true;
}

void VoxelSystem::randomizeVoxelColors() {
    _nodeCount = 0;
    _tree->recurseTreeWithOperation(randomColorOperation);
    printLog("setting randomized true color for %d nodes\n", _nodeCount);
    setupNewVoxelsForDrawing();
}

bool VoxelSystem::falseColorizeRandomOperation(VoxelNode* node, void* extraData) {
    _nodeCount++;
    // always false colorize
    node->setFalseColor(255, randomColorValue(150), randomColorValue(150));
    return true; // keep going!
}

void VoxelSystem::falseColorizeRandom() {
    _nodeCount = 0;
    _tree->recurseTreeWithOperation(falseColorizeRandomOperation);
    printLog("setting randomized false color for %d nodes\n", _nodeCount);
    setupNewVoxelsForDrawing();
}

bool VoxelSystem::trueColorizeOperation(VoxelNode* node, void* extraData) {
    _nodeCount++;
    node->setFalseColored(false);
    return true;
}

void VoxelSystem::trueColorize() {
    PerformanceWarning warn(true, "trueColorize()",true);
    _nodeCount = 0;
    _tree->recurseTreeWithOperation(trueColorizeOperation);
    printLog("setting true color for %d nodes\n", _nodeCount);
    setupNewVoxelsForDrawing();
}

// Will false colorize voxels that are not in view
bool VoxelSystem::falseColorizeInViewOperation(VoxelNode* node, void* extraData) {
    const ViewFrustum* viewFrustum = (const ViewFrustum*) extraData;
    _nodeCount++;
    if (node->isColored()) {
        if (!node->isInView(*viewFrustum)) {
            // Out of view voxels are colored RED
            node->setFalseColor(255, 0, 0);
        }
    }
    return true; // keep going!
}

void VoxelSystem::falseColorizeInView(ViewFrustum* viewFrustum) {
    _nodeCount = 0;
    _tree->recurseTreeWithOperation(falseColorizeInViewOperation,(void*)viewFrustum);
    printLog("setting in view false color for %d nodes\n", _nodeCount);
    setupNewVoxelsForDrawing();
}

// Will false colorize voxels based on distance from view
bool VoxelSystem::falseColorizeDistanceFromViewOperation(VoxelNode* node, void* extraData) {
    ViewFrustum* viewFrustum = (ViewFrustum*) extraData;
    if (node->isColored()) {
        float distance = node->distanceToCamera(*viewFrustum);
        _nodeCount++;
        float distanceRatio = (_minDistance == _maxDistance) ? 1 : (distance - _minDistance) / (_maxDistance - _minDistance);

        // We want to colorize this in 16 bug chunks of color
        const unsigned char maxColor = 255;
        const unsigned char colorBands = 16;
        const unsigned char gradientOver = 128;
        unsigned char colorBand = (colorBands * distanceRatio);
        node->setFalseColor((colorBand * (gradientOver / colorBands)) + (maxColor - gradientOver), 0, 0);
    }
    return true; // keep going!
}

float VoxelSystem::_maxDistance = 0.0;
float VoxelSystem::_minDistance = FLT_MAX;

// Helper function will get the distance from view range, would be nice if you could just keep track
// of this as voxels are created and/or colored... seems like some transform math could do that so
// we wouldn't need to do two passes of the tree
bool VoxelSystem::getDistanceFromViewRangeOperation(VoxelNode* node, void* extraData) {
    ViewFrustum* viewFrustum = (ViewFrustum*) extraData;
    // only do this for truly colored voxels...
    if (node->isColored()) {
        float distance = node->distanceToCamera(*viewFrustum);
        // calculate the range of distances
        if (distance > _maxDistance) {
            _maxDistance = distance;
        }
        if (distance < _minDistance) {
            _minDistance = distance;
        }
        _nodeCount++;
    }
    return true; // keep going!
}

void VoxelSystem::falseColorizeDistanceFromView(ViewFrustum* viewFrustum) {
    _nodeCount = 0;
    _maxDistance = 0.0;
    _minDistance = FLT_MAX;
    _tree->recurseTreeWithOperation(getDistanceFromViewRangeOperation,(void*)viewFrustum);
    printLog("determining distance range for %d nodes\n", _nodeCount);
    _nodeCount = 0;
    _tree->recurseTreeWithOperation(falseColorizeDistanceFromViewOperation,(void*)viewFrustum);
    printLog("setting in distance false color for %d nodes\n", _nodeCount);
    setupNewVoxelsForDrawing();
}

// combines the removeOutOfView args into a single class
class removeOutOfViewArgs {
public:
    VoxelSystem*    thisVoxelSystem;
    VoxelNodeBag    dontRecurseBag;
    unsigned long   nodesScanned;
    unsigned long   nodesRemoved;
    unsigned long   nodesInside;
    unsigned long   nodesIntersect;
    unsigned long   nodesOutside;
    
    removeOutOfViewArgs(VoxelSystem* voxelSystem) :
        thisVoxelSystem(voxelSystem),
        dontRecurseBag(),
        nodesScanned(0),
        nodesRemoved(0),
        nodesInside(0),
        nodesIntersect(0),
        nodesOutside(0)
    { }
};

// "Remove" voxels from the tree that are not in view. We don't actually delete them,
// we remove them from the tree and place them into a holding area for later deletion
bool VoxelSystem::removeOutOfViewOperation(VoxelNode* node, void* extraData) {
    removeOutOfViewArgs* args = (removeOutOfViewArgs*)extraData;

    // If our node was previously added to the don't recurse bag, then return false to
    // stop the further recursion. This means that the whole node and it's children are
    // known to be in view, so don't recurse them
    if (args->dontRecurseBag.contains(node)) {
        args->dontRecurseBag.remove(node);
        return false; // stop recursion
    }
    
    VoxelSystem* thisVoxelSystem = args->thisVoxelSystem;
    args->nodesScanned++;
    // Need to operate on our child nodes, so we can remove them
    for (int i = 0; i < NUMBER_OF_CHILDREN; i++) {
        VoxelNode* childNode = node->getChildAtIndex(i);
        if (childNode) {
            ViewFrustum::location inFrustum = childNode->inFrustum(*Application::getInstance()->getViewFrustum());
            switch (inFrustum) {
                case ViewFrustum::OUTSIDE: {
                    args->nodesOutside++;
                    args->nodesRemoved++;
                    node->removeChildAtIndex(i);
                    thisVoxelSystem->_removedVoxels.insert(childNode);
                    // by removing the child, it will not get recursed!
                } break;
                case ViewFrustum::INSIDE: {
                    // if the child node is fully INSIDE the view, then there's no need to recurse it
                    // because we know all it's children will also be in the view, so we want to 
                    // tell the caller to NOT recurse this child
                    args->nodesInside++;
                    args->dontRecurseBag.insert(childNode);
                } break;
                case ViewFrustum::INTERSECT: {
                    // if the child node INTERSECTs the view, then we don't want to remove it because
                    // it is at least partially in view. But we DO want to recurse the children because
                    // some of them may not be in view... nothing specifically to do, just keep iterating
                    // the children
                    args->nodesIntersect++;
                } break;
            }
        }
    }
    return true; // keep going!
}


bool VoxelSystem::isViewChanging() {
    bool result = false; // assume the best

/** TEMPORARY HACK ******
    // If our viewFrustum has changed since our _lastKnowViewFrustum
    if (_viewFrustum && !_lastKnowViewFrustum.matches(_viewFrustum)) {
        result = true;
        _lastKnowViewFrustum = *_viewFrustum; // save last known
    }
**/
    return result;
}

bool VoxelSystem::hasViewChanged() {
    bool result = false; // assume the best
    
    // If we're still changing, report no change yet.
    if (isViewChanging()) {
        return false;
    }
    
    // If our viewFrustum has changed since our _lastKnowViewFrustum
    if (!_lastStableViewFrustum.matches(Application::getInstance()->getViewFrustum())) {
        result = true;
        _lastStableViewFrustum = *Application::getInstance()->getViewFrustum(); // save last stable
    }
    return result;
}

void VoxelSystem::removeOutOfView() {
    PerformanceWarning warn(_renderWarningsOn, "removeOutOfView()");
    removeOutOfViewArgs args(this);
    _tree->recurseTreeWithOperation(removeOutOfViewOperation,(void*)&args);

    if (args.nodesRemoved) {
        _tree->setDirtyBit();
    }
    bool showRemoveDebugDetails = false;
    if (showRemoveDebugDetails) {
        printLog("removeOutOfView() scanned=%ld removed=%ld inside=%ld intersect=%ld outside=%ld _removedVoxels.count()=%d \n", 
                args.nodesScanned, args.nodesRemoved, args.nodesInside, 
                args.nodesIntersect, args.nodesOutside, _removedVoxels.count()
            );
    }
}

bool VoxelSystem::findRayIntersection(const glm::vec3& origin, const glm::vec3& direction,
                                      VoxelDetail& detail, float& distance, BoxFace& face) {
    pthread_mutex_lock(&_treeLock);                                  
    VoxelNode* node;
    if (!_tree->findRayIntersection(origin, direction, node, distance, face)) {
        pthread_mutex_unlock(&_treeLock);
        return false;
    }
    detail.x = node->getCorner().x;
    detail.y = node->getCorner().y;
    detail.z = node->getCorner().z;
    detail.s = node->getScale();
    detail.red = node->getColor()[0];
    detail.green = node->getColor()[1];
    detail.blue = node->getColor()[2];
    pthread_mutex_unlock(&_treeLock);
    return true;
}

bool VoxelSystem::findSpherePenetration(const glm::vec3& center, float radius, glm::vec3& penetration) {
    pthread_mutex_lock(&_treeLock);
    bool result = _tree->findSpherePenetration(center, radius, penetration);
    pthread_mutex_unlock(&_treeLock);
    return result;
}

bool VoxelSystem::findCapsulePenetration(const glm::vec3& start, const glm::vec3& end, float radius, glm::vec3& penetration) {
    pthread_mutex_lock(&_treeLock);
    bool result = _tree->findCapsulePenetration(start, end, radius, penetration);
    pthread_mutex_unlock(&_treeLock);
    return result;
}

class falseColorizeRandomEveryOtherArgs {
public:
    falseColorizeRandomEveryOtherArgs() : totalNodes(0), colorableNodes(0), coloredNodes(0), colorThis(true) {};
    unsigned long totalNodes;
    unsigned long colorableNodes;
    unsigned long coloredNodes;
    bool colorThis;
};

bool VoxelSystem::falseColorizeRandomEveryOtherOperation(VoxelNode* node, void* extraData) {
    falseColorizeRandomEveryOtherArgs* args = (falseColorizeRandomEveryOtherArgs*)extraData;
    args->totalNodes++;
    if (node->isColored()) {
        args->colorableNodes++;
        if (args->colorThis) {
            args->coloredNodes++;
            node->setFalseColor(255, randomColorValue(150), randomColorValue(150));
        }
        args->colorThis = !args->colorThis;
    }
    return true; // keep going!
}

void VoxelSystem::falseColorizeRandomEveryOther() {
    falseColorizeRandomEveryOtherArgs args;
    _tree->recurseTreeWithOperation(falseColorizeRandomEveryOtherOperation,&args);
    printLog("randomized false color for every other node: total %ld, colorable %ld, colored %ld\n", 
        args.totalNodes, args.colorableNodes, args.coloredNodes);
    setupNewVoxelsForDrawing();
}

class collectStatsForTreesAndVBOsArgs {
public:
    collectStatsForTreesAndVBOsArgs() : 
        totalNodes(0), 
        dirtyNodes(0), 
        shouldRenderNodes(0),
        coloredNodes(0),
        nodesInVBO(0),
        nodesInVBONotShouldRender(0),
        nodesInVBOOverExpectedMax(0),
        duplicateVBOIndex(0),
        leafNodes(0)
        {
            memset(hasIndexFound, false, MAX_VOXELS_PER_SYSTEM * sizeof(bool));
        };

    unsigned long totalNodes;
    unsigned long dirtyNodes;
    unsigned long shouldRenderNodes;
    unsigned long coloredNodes;
    unsigned long nodesInVBO;
    unsigned long nodesInVBONotShouldRender;
    unsigned long nodesInVBOOverExpectedMax;
    unsigned long duplicateVBOIndex;
    unsigned long leafNodes;

    unsigned long expectedMax;
    
    bool hasIndexFound[MAX_VOXELS_PER_SYSTEM];
};

bool VoxelSystem::collectStatsForTreesAndVBOsOperation(VoxelNode* node, void* extraData) {
    collectStatsForTreesAndVBOsArgs* args = (collectStatsForTreesAndVBOsArgs*)extraData;
    args->totalNodes++;

    if (node->isLeaf()) {
        args->leafNodes++;
    }

    if (node->isColored()) {
        args->coloredNodes++;
    }

    if (node->getShouldRender()) {
        args->shouldRenderNodes++;
    }

    if (node->isDirty()) {
        args->dirtyNodes++;
    }

    if (node->isKnownBufferIndex()) {
        args->nodesInVBO++;
        unsigned long nodeIndex = node->getBufferIndex();
        if (args->hasIndexFound[nodeIndex]) {
            args->duplicateVBOIndex++;
            printLog("duplicateVBO found... index=%ld, isDirty=%s, shouldRender=%s \n", nodeIndex, 
                    debug::valueOf(node->isDirty()), debug::valueOf(node->getShouldRender()));
        } else {
            args->hasIndexFound[nodeIndex] = true;
        }
        if (nodeIndex > args->expectedMax) {
            args->nodesInVBOOverExpectedMax++;
        }
        
        // if it's in VBO but not-shouldRender, track that also...
        if (!node->getShouldRender()) {
            args->nodesInVBONotShouldRender++;
        }
    }

    return true; // keep going!
}

void VoxelSystem::collectStatsForTreesAndVBOs() {
    PerformanceWarning warn(true, "collectStatsForTreesAndVBOs()", true);

    glBufferIndex minDirty = GLBUFFER_INDEX_UNKNOWN;
    glBufferIndex maxDirty = 0;

    for (glBufferIndex i = 0; i < _voxelsInWriteArrays; i++) {
        if (_writeVoxelDirtyArray[i]) {
            minDirty = std::min(minDirty,i);
            maxDirty = std::max(maxDirty,i);
        }
    }

    collectStatsForTreesAndVBOsArgs args;
    args.expectedMax = _voxelsInWriteArrays;
    _tree->recurseTreeWithOperation(collectStatsForTreesAndVBOsOperation,&args);

    printLog("_voxelsDirty=%s _voxelsInWriteArrays=%ld minDirty=%ld maxDirty=%ld \n", debug::valueOf(_voxelsDirty),
        _voxelsInWriteArrays, minDirty, maxDirty);

    printLog("stats: total %ld, leaves %ld, dirty %ld, colored %ld, shouldRender %ld, inVBO %ld\n",
        args.totalNodes, args.leafNodes, args.dirtyNodes, args.coloredNodes, args.shouldRenderNodes);

    printLog("inVBO %ld, nodesInVBOOverExpectedMax %ld, duplicateVBOIndex %ld, nodesInVBONotShouldRender %ld\n", 
        args.nodesInVBO, args.nodesInVBOOverExpectedMax, args.duplicateVBOIndex, args.nodesInVBONotShouldRender);

    glBufferIndex minInVBO = GLBUFFER_INDEX_UNKNOWN;
    glBufferIndex maxInVBO = 0;

    for (glBufferIndex i = 0; i < MAX_VOXELS_PER_SYSTEM; i++) {
        if (args.hasIndexFound[i]) {
            minInVBO = std::min(minInVBO,i);
            maxInVBO = std::max(maxInVBO,i);
        }
    }

    printLog("minInVBO=%ld maxInVBO=%ld _voxelsInWriteArrays=%ld _voxelsInReadArrays=%ld\n", 
            minInVBO, maxInVBO, _voxelsInWriteArrays, _voxelsInReadArrays);

}


void VoxelSystem::deleteVoxelAt(float x, float y, float z, float s) {
    pthread_mutex_lock(&_treeLock);
    
    _tree->deleteVoxelAt(x, y, z, s, true);
    
    // redraw!
    setupNewVoxelsForDrawing();  // do we even need to do this? Or will the next network receive kick in?
    
    pthread_mutex_unlock(&_treeLock);
};

VoxelNode* VoxelSystem::getVoxelAt(float x, float y, float z, float s) const { 
    return _tree->getVoxelAt(x, y, z, s); 
};

void VoxelSystem::createVoxel(float x, float y, float z, float s, 
                              unsigned char red, unsigned char green, unsigned char blue, bool destructive) {
    pthread_mutex_lock(&_treeLock);
    
    //printLog("VoxelSystem::createVoxel(%f,%f,%f,%f)\n",x,y,z,s);
    _tree->createVoxel(x, y, z, s, red, green, blue, destructive); 
    setupNewVoxelsForDrawing(); 
    
    pthread_mutex_unlock(&_treeLock);
};

void VoxelSystem::createLine(glm::vec3 point1, glm::vec3 point2, float unitSize, rgbColor color, bool destructive) { 
    _tree->createLine(point1, point2, unitSize, color, destructive); 
    setupNewVoxelsForDrawing(); 
};

void VoxelSystem::createSphere(float r,float xc, float yc, float zc, float s, bool solid, 
                               creationMode mode, bool destructive, bool debug) { 
    _tree->createSphere(r, xc, yc, zc, s, solid, mode, destructive, debug); 
    setupNewVoxelsForDrawing(); 
};

void VoxelSystem::copySubTreeIntoNewTree(VoxelNode* startNode, VoxelTree* destinationTree, bool rebaseToRoot) {
    _tree->copySubTreeIntoNewTree(startNode, destinationTree, rebaseToRoot);
}

void VoxelSystem::copyFromTreeIntoSubTree(VoxelTree* sourceTree, VoxelNode* destinationNode) {
    _tree->copyFromTreeIntoSubTree(sourceTree, destinationNode);
}

struct FalseColorizeOccludedArgs {
    VoxelProjectedShadow occluder;
    ViewFrustum* viewFrustum;
    CoverageMap* map;
};

bool VoxelSystem::falseColorizeTestOccludedOperation(VoxelNode* node, void* extraData) {
    FalseColorizeOccludedArgs* args = (FalseColorizeOccludedArgs*) extraData;
    if (node->isColored()) {
        AABox voxelBox = node->getAABox();
        voxelBox.scale(TREE_SCALE);
        VoxelProjectedShadow voxelShadow = args->viewFrustum->getProjectedShadow(voxelBox);
        if (args->occluder.occludes(voxelShadow)) {
            node->setFalseColor(255, 0, 0);
        }
    }
    return true; // keep going!
}

void VoxelSystem::falseColorizeTestOccluded() {
    FalseColorizeOccludedArgs args;
    args.viewFrustum = Application::getInstance()->getViewFrustum();
    
    AABox box(glm::vec3(0.0125,0,0.025), 0.0125);
    box.scale(TREE_SCALE);
    args.occluder = args.viewFrustum->getProjectedShadow(box);

    _tree->recurseTreeWithOperation(falseColorizeTestOccludedOperation,(void*)&args);
    setupNewVoxelsForDrawing();
}

bool VoxelSystem::falseColorizeOccludedOperation(VoxelNode* node, void* extraData) {
    FalseColorizeOccludedArgs* args = (FalseColorizeOccludedArgs*) extraData;
    if (node->isColored() && node->isLeaf()) {
        AABox voxelBox = node->getAABox();
        voxelBox.scale(TREE_SCALE);
        VoxelProjectedShadow* voxelShadow = new VoxelProjectedShadow(args->viewFrustum->getProjectedShadow(voxelBox));
        CoverageMap::StorageResult result = args->map->storeInMap(voxelShadow);
        if (result == CoverageMap::OCCLUDED) {
            node->setFalseColor(255, 0, 0);
        }
    }
    return true; // keep going!
}

void VoxelSystem::falseColorizeOccluded() {
    CoverageMap map(BoundingBox(glm::vec2(-1.f,-1.f), glm::vec2(2.f,2.f)), true);
    FalseColorizeOccludedArgs args;
    args.viewFrustum = Application::getInstance()->getViewFrustum();
    args.map = &map; 
    
    AABox box(glm::vec3(0.0125,0,0.025), 0.0125);
    box.scale(TREE_SCALE);
    args.occluder = args.viewFrustum->getProjectedShadow(box);

    _tree->recurseTreeWithOperationDistanceSorted(falseColorizeOccludedOperation, 
                args.viewFrustum->getPosition(), (void*)&args);
    setupNewVoxelsForDrawing();
}

