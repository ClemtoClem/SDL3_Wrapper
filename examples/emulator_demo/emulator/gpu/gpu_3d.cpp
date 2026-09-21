#include <cstring>
#include "emulator/gpu/gpu_3d.hpp"
#include "emulator/core.hpp"
#include "emulator/settings.hpp"

namespace emulator_demo {


Matrix Matrix::operator*(Matrix &mtx) {
    Matrix result;
    for (int y = 0; y < 4; y++) {
        for (int x = 0; x < 4; x++) {
            result.data[y * 4 + x] =
                ((int64_t)data[y * 4 + 0] * mtx.data[0 + x] + (int64_t)data[y * 4 + 1] * mtx.data[4 + x] +
                 (int64_t)data[y * 4 + 2] * mtx.data[8 + x] + (int64_t)data[y * 4 + 3] * mtx.data[12 + x]) >>
                12;
        }
    }
    return result;
}

int32_t Vector::operator*(Vector &vtr) {
    return ((int64_t)x * vtr.x + (int64_t)y * vtr.y + (int64_t)z * vtr.z) >> 12;
}

Vector Vector::operator*(Matrix &mtx) {
    Vector result;
    result.x = ((int64_t)x * mtx.data[0] + (int64_t)y * mtx.data[4] + (int64_t)z * mtx.data[8]) >> 12;
    result.y = ((int64_t)x * mtx.data[1] + (int64_t)y * mtx.data[5] + (int64_t)z * mtx.data[9]) >> 12;
    result.z = ((int64_t)x * mtx.data[2] + (int64_t)y * mtx.data[6] + (int64_t)z * mtx.data[10]) >> 12;
    return result;
}

Vertex Vertex::operator*(Matrix &mtx) {
    Vertex result = *this;
    result.x =
        ((int64_t)x * mtx.data[0] + (int64_t)y * mtx.data[4] + (int64_t)z * mtx.data[8] + (int64_t)w * mtx.data[12]) >>
        12;
    result.y =
        ((int64_t)x * mtx.data[1] + (int64_t)y * mtx.data[5] + (int64_t)z * mtx.data[9] + (int64_t)w * mtx.data[13]) >>
        12;
    result.z =
        ((int64_t)x * mtx.data[2] + (int64_t)y * mtx.data[6] + (int64_t)z * mtx.data[10] + (int64_t)w * mtx.data[14]) >>
        12;
    result.w =
        ((int64_t)x * mtx.data[3] + (int64_t)y * mtx.data[7] + (int64_t)z * mtx.data[11] + (int64_t)w * mtx.data[15]) >>
        12;
    return result;
}

const uint8_t Gpu3D::paramCounts[] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 1, 1, 1,  0, 16, 12, 16, 12, 9, 3, 3, 0, 0, 0,
    1, 1, 1, 2, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 32, 0, 0,  0,  0,  0,  0, 0, 0, 0, 0, 0,
    1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0,  0, 0,  0,  0,  0,  0, 0, 0, 0, 0, 0,
    1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 2, 1, 0, 0,  0, 0,  0,  0,  0,  0, 0, 0, 0, 0, 0,
};

Gpu3D::Gpu3D(Core *core) : core(core) {
    runCommandTask = std::bind(&Gpu3D::runCommand, this);
}

uint32_t Gpu3D::rgb5ToRgb6(uint16_t color) {
    uint8_t r = ((color >> 0) & 0x1F) * 2;
    if (r > 0)
        r++;
    uint8_t g = ((color >> 5) & 0x1F) * 2;
    if (g > 0)
        g++;
    uint8_t b = ((color >> 10) & 0x1F) * 2;
    if (b > 0)
        b++;
    return (b << 12) | (g << 6) | r;
}

Vertex Gpu3D::intersection(Vertex *vtx1, Vertex *vtx2, int32_t val1, int32_t val2) {
    Vertex  vertex;
    int64_t d1 = val1 + vtx1->w;
    int64_t d2 = val2 + vtx2->w;
    if (d2 == d1)
        return *vtx1;
    vertex.x     = ((d2 * vtx1->x) - (d1 * vtx2->x)) / (d2 - d1);
    vertex.y     = ((d2 * vtx1->y) - (d1 * vtx2->y)) / (d2 - d1);
    vertex.z     = ((d2 * vtx1->z) - (d1 * vtx2->z)) / (d2 - d1);
    vertex.w     = ((d2 * vtx1->w) - (d1 * vtx2->w)) / (d2 - d1);
    vertex.s     = ((d2 * vtx1->s) - (d1 * vtx2->s)) / (d2 - d1);
    vertex.t     = ((d2 * vtx1->t) - (d1 * vtx2->t)) / (d2 - d1);
    uint8_t r    = ((d2 * ((vtx1->color >> 0) & 0x3F)) - (d1 * ((vtx2->color >> 0) & 0x3F))) / (d2 - d1);
    uint8_t g    = ((d2 * ((vtx1->color >> 6) & 0x3F)) - (d1 * ((vtx2->color >> 6) & 0x3F))) / (d2 - d1);
    uint8_t b    = ((d2 * ((vtx1->color >> 12) & 0x3F)) - (d1 * ((vtx2->color >> 12) & 0x3F))) / (d2 - d1);
    vertex.color = (vtx1->color & 0xFC0000) | (b << 12) | (g << 6) | r;
    return vertex;
}

bool Gpu3D::clipPolygon(Vertex *unclipped, Vertex *clipped, int *size) {
    bool   clip = false;
    Vertex vertices[10];
    memcpy(vertices, unclipped, *size * sizeof(Vertex));
    for (int i = 0; i < 6; i++) {
        int oldSize = *size;
        *size       = 0;
        for (int j = 0; j < oldSize; j++) {
            Vertex *current  = &vertices[j];
            Vertex *previous = &vertices[(j - 1 + oldSize) % oldSize];
            int32_t currentVal, previousVal;
            switch (i) {
                case 0:
                    currentVal  = current->x;
                    previousVal = previous->x;
                    break;
                case 1:
                    currentVal  = -current->x;
                    previousVal = -previous->x;
                    break;
                case 2:
                    currentVal  = current->y;
                    previousVal = previous->y;
                    break;
                case 3:
                    currentVal  = -current->y;
                    previousVal = -previous->y;
                    break;
                case 4:
                    currentVal  = current->z;
                    previousVal = previous->z;
                    break;
                case 5:
                    currentVal  = -current->z;
                    previousVal = -previous->z;
                    break;
            }
            if (currentVal >= -current->w) {
                if (previousVal < -previous->w) {
                    clipped[(*size)++] = intersection(current, previous, currentVal, previousVal);
                    clip               = true;
                }
                clipped[(*size)++] = *current;
            } else if (previousVal >= -previous->w) {
                clipped[(*size)++] = intersection(current, previous, currentVal, previousVal);
                clip               = true;
            }
        }
        memcpy(vertices, clipped, *size * sizeof(Vertex));
    }
    return clip;
}

void Gpu3D::runCommand() {
    Entry                 entry = fifo.front();
    int                   count = paramCounts[entry.command];
    std::vector<uint32_t> params;
    if (count > 1) {
        params.reserve(count);
        for (int i = 0; i < count; i++) {
            params.push_back(fifo.front().param);
            fifo.pop();
        }
    } else {
        count = 1;
        fifo.pop();
    }
    switch (entry.command) {
        case 0x10:
            mtxModeCmd(entry.param);
            break;
        case 0x11:
            mtxPushCmd();
            break;
        case 0x12:
            mtxPopCmd(entry.param);
            break;
        case 0x13:
            mtxStoreCmd(entry.param);
            break;
        case 0x14:
            mtxRestoreCmd(entry.param);
            break;
        case 0x15:
            mtxIdentityCmd();
            break;
        case 0x16:
            mtxLoad44Cmd(params);
            break;
        case 0x17:
            mtxLoad43Cmd(params);
            break;
        case 0x18:
            mtxMult44Cmd(params);
            break;
        case 0x19:
            mtxMult43Cmd(params);
            break;
        case 0x1A:
            mtxMult33Cmd(params);
            break;
        case 0x1B:
            mtxScaleCmd(params);
            break;
        case 0x1C:
            mtxTransCmd(params);
            break;
        case 0x20:
            colorCmd(entry.param);
            break;
        case 0x21:
            normalCmd(entry.param);
            break;
        case 0x22:
            texCoordCmd(entry.param);
            break;
        case 0x23:
            vtx16Cmd(params);
            break;
        case 0x24:
            vtx10Cmd(entry.param);
            break;
        case 0x25:
            vtxXYCmd(entry.param);
            break;
        case 0x26:
            vtxXZCmd(entry.param);
            break;
        case 0x27:
            vtxYZCmd(entry.param);
            break;
        case 0x28:
            vtxDiffCmd(entry.param);
            break;
        case 0x29:
            polygonAttrCmd(entry.param);
            break;
        case 0x2A:
            texImageParamCmd(entry.param);
            break;
        case 0x2B:
            plttBaseCmd(entry.param);
            break;
        case 0x30:
            difAmbCmd(entry.param);
            break;
        case 0x31:
            speEmiCmd(entry.param);
            break;
        case 0x32:
            lightVectorCmd(entry.param);
            break;
        case 0x33:
            lightColorCmd(entry.param);
            break;
        case 0x34:
            shininessCmd(params);
            break;
        case 0x40:
            beginVtxsCmd(entry.param);
            break;
        case 0x41:
            break;
        case 0x50:
            swapBuffersCmd(entry.param);
            break;
        case 0x60:
            viewportCmd(entry.param);
            break;
        case 0x70:
            boxTestCmd(params);
            break;
        case 0x71:
            posTestCmd(params);
            break;
        case 0x72:
            vecTestCmd(entry.param);
            break;
        default: {
            LOG_VERBOSE("Unknown GXFIFO command: 0x%X\n", entry.command);
            break;
        }
    }
    pipeSize = 4 - ((pipeSize + count) & 1);
    if (pipeSize > fifo.size())
        pipeSize = fifo.size();
    gxStat = (gxStat & ~0x01FF0000) | ((fifo.size() - pipeSize) << 16);
    if (fifo.size() - pipeSize == 0)
        gxStat |= BIT(26);
    if (fifo.size() == 0)
        gxStat &= ~BIT(27);
    if (fifo.size() - pipeSize < 128 && !(gxStat & BIT(25))) {
        gxStat |= BIT(25);
        core->dma[0].trigger(7);
    }
    switch ((gxStat & 0xC0000000) >> 30) {
        case 1:
            if (gxStat & BIT(25))
                core->interpreter[0].sendInterrupt(21);
            break;
        case 2:
            if (gxStat & BIT(26))
                core->interpreter[0].sendInterrupt(21);
            break;
    }
    if (fifo.size() - pipeSize <= 256)
        core->interpreter[0].unhalt(1);
    if (state != GX_HALTED) {
        if (!fifo.empty() && fifo.size() >= paramCounts[fifo.front().command])
            core->schedule(Task(&runCommandTask, 2));
        else
            state = GX_IDLE;
    }
}

void Gpu3D::swapBuffers() {
    bool     resShift = Settings::getHighRes3D();
    uint16_t x        = viewportX << resShift;
    uint16_t y        = viewportY << resShift;
    uint16_t w        = viewportWidth << resShift;
    uint16_t h        = viewportHeight << resShift;
    uint16_t xMask    = (0x200 << resShift) - 1;
    uint16_t yMask    = (0x100 << resShift) - 1;
    for (int i = 0; i < vertexCountIn; i++) {
        if (verticesIn[i].w != 0) {
            verticesIn[i].x = (((int64_t)verticesIn[i].x + verticesIn[i].w) * w / (verticesIn[i].w * 2) + x) & xMask;
            verticesIn[i].y = ((-(int64_t)verticesIn[i].y + verticesIn[i].w) * h / (verticesIn[i].w * 2) + y) & yMask;
            verticesIn[i].z = (((((int64_t)verticesIn[i].z << 14) / verticesIn[i].w) + 0x3FFF) << 9);
        }
    }
    for (int i = 0; i < polygonCountIn; i++) {
        _Polygon *p     = &polygonsIn[i];
        uint32_t  value = p->vertices[0].w;
        for (int j = 1; j < p->size; j++) {
            if ((uint32_t)p->vertices[j].w > value)
                value = p->vertices[j].w;
        }
        while ((value >> p->wShift) > 0xFFFF)
            p->wShift += 4;
        if (p->wShift == 0 && value != 0) {
            while ((value << -(p->wShift - 4)) <= 0xFFFF)
                p->wShift -= 4;
        }
    }
    SWAP(verticesOut, verticesIn);
    vertexCountOut = vertexCountIn;
    vertexCountIn  = 0;
    vertexCount    = 0;
    SWAP(polygonsOut, polygonsIn);
    polygonCountOut = polygonCountIn;
    polygonCountIn  = 0;
    core->gpu.invalidate3D();
    if (!fifo.empty() && fifo.size() >= paramCounts[fifo.front().command]) {
        core->schedule(Task(&runCommandTask, 2));
        state = GX_RUNNING;
    } else {
        state = GX_IDLE;
    }
}

void Gpu3D::addVertex() {
    if (vertexCountIn >= 6144)
        return;
    verticesIn[vertexCountIn]   = savedVertex;
    verticesIn[vertexCountIn].w = 1 << 12;
    if (textureCoordMode == 3) {
        Matrix matrix               = texture;
        matrix.data[12]             = (int32_t)s << 12;
        matrix.data[13]             = (int32_t)t << 12;
        Vertex vertex               = verticesIn[vertexCountIn] * matrix;
        verticesIn[vertexCountIn].s = vertex.x >> 12;
        verticesIn[vertexCountIn].t = vertex.y >> 12;
    }
    if (clipDirty) {
        clip      = coordinate * projection;
        clipDirty = false;
    }
    verticesIn[vertexCountIn] = verticesIn[vertexCountIn] * clip;
    vertexCountIn++;
    vertexCount++;
    switch (polygonType) {
        case 0:
            if (vertexCount % 3 == 0)
                addPolygon();
            break;
        case 1:
            if (vertexCount % 4 == 0)
                addPolygon();
            break;
        case 2:
            if (vertexCount >= 3)
                addPolygon();
            break;
        case 3:
            if (vertexCount >= 4 && vertexCount % 2 == 0)
                addPolygon();
            break;
    }
}

void Gpu3D::addPolygon() {
    if (polygonCountIn >= 2048)
        return;
    int size              = 3 + (polygonType & 1);
    savedPolygon.size     = size;
    savedPolygon.vertices = &verticesIn[vertexCountIn - size];
    Vertex unclipped[4];
    memcpy(unclipped, savedPolygon.vertices, size * sizeof(Vertex));
    if (polygonType == 3)
        SWAP(unclipped[2], unclipped[3]);
    int64_t x1 = unclipped[1].x - unclipped[0].x;
    int64_t y1 = unclipped[1].y - unclipped[0].y;
    int64_t w1 = unclipped[1].w - unclipped[0].w;
    int64_t x2 = unclipped[2].x - unclipped[0].x;
    int64_t y2 = unclipped[2].y - unclipped[0].y;
    int64_t w2 = unclipped[2].w - unclipped[0].w;
    int64_t xc = y1 * w2 - w1 * y2;
    int64_t yc = w1 * x2 - x1 * w2;
    int64_t wc = x1 * y2 - y1 * x2;
    while (xc != (int32_t)xc || yc != (int32_t)yc || wc != (int32_t)wc) {
        xc >>= 4;
        yc >>= 4;
        wc >>= 4;
    }
    int64_t dot            = xc * unclipped[0].x + yc * unclipped[0].y + wc * unclipped[0].w;
    savedPolygon.clockwise = (dot < 0);
    if (polygonType == 2) {
        if (clockwise)
            dot = -dot;
        clockwise = !clockwise;
    }
    Vertex clipped[10];
    bool   cull = (!renderFront && dot > 0) || (!renderBack && dot < 0);
    bool   clip = cull ? false : clipPolygon(unclipped, clipped, &savedPolygon.size);
    if (cull || savedPolygon.size == 0) {
        switch (polygonType) {
            case 0:
            case 1: {
                vertexCountIn -= size;
                return;
            }
            case 2: {
                if (vertexCount == 3) {
                    verticesIn[vertexCountIn - 3] = verticesIn[vertexCountIn - 2];
                    verticesIn[vertexCountIn - 2] = verticesIn[vertexCountIn - 1];
                    vertexCountIn--;
                    vertexCount--;
                } else if (vertexCountIn < 6144) {
                    verticesIn[vertexCountIn - 0] = verticesIn[vertexCountIn - 1];
                    verticesIn[vertexCountIn - 1] = verticesIn[vertexCountIn - 2];
                    vertexCountIn++;
                    vertexCount = 2;
                }
                return;
            }
            case 3: {
                if (vertexCount == 4) {
                    verticesIn[vertexCountIn - 4] = verticesIn[vertexCountIn - 2];
                    verticesIn[vertexCountIn - 3] = verticesIn[vertexCountIn - 1];
                    vertexCountIn -= 2;
                    vertexCount -= 2;
                } else {
                    vertexCount = 2;
                }
                return;
            }
        }
    }
    if (clip) {
        switch (polygonType) {
            case 0:
            case 1: {
                vertexCountIn -= size;
                for (int i = 0; i < savedPolygon.size; i++) {
                    if (vertexCountIn >= 6144)
                        return;
                    verticesIn[vertexCountIn] = clipped[i];
                    vertexCountIn++;
                }
                break;
            }
            case 2: {
                vertexCountIn -= (vertexCount == 3) ? 3 : 1;
                savedPolygon.vertices = &verticesIn[vertexCountIn];
                for (int i = 0; i < savedPolygon.size; i++) {
                    if (vertexCountIn >= 6144)
                        return;
                    verticesIn[vertexCountIn] = clipped[i];
                    vertexCountIn++;
                }
                for (int i = 0; i < 2; i++) {
                    if (vertexCountIn >= 6144)
                        return;
                    verticesIn[vertexCountIn] = unclipped[1 + i];
                    vertexCountIn++;
                }
                vertexCount = 2;
                break;
            }
            case 3: {
                vertexCountIn -= (vertexCount == 4) ? 4 : 2;
                savedPolygon.vertices = &verticesIn[vertexCountIn];
                for (int i = 0; i < savedPolygon.size; i++) {
                    if (vertexCountIn >= 6144)
                        return;
                    verticesIn[vertexCountIn] = clipped[i];
                    vertexCountIn++;
                }
                for (int i = 0; i < 2; i++) {
                    if (vertexCountIn >= 6144)
                        return;
                    verticesIn[vertexCountIn] = unclipped[3 - i];
                    vertexCountIn++;
                }
                vertexCount = 2;
                break;
            }
        }
    }
    polygonsIn[polygonCountIn]         = savedPolygon;
    polygonsIn[polygonCountIn].crossed = (polygonType == 3 && !clip);
    polygonsIn[polygonCountIn].paletteAddr <<= 4 - (savedPolygon.textureFmt == 2);
    polygonCountIn++;
}

void Gpu3D::mtxModeCmd(uint32_t param) {
    matrixMode = param & 0x00000003;
}

void Gpu3D::mtxPushCmd() {
    switch (matrixMode) {
        case 0: {
            if (!(gxStat & BIT(13))) {
                projectionStack = projection;
                gxStat |= BIT(13);
            } else {
                gxStat |= BIT(15);
            }
            break;
        }
        case 1:
        case 2: {
            uint8_t pointer = (gxStat >> 8) & 0x1F;
            if (pointer >= 30)
                gxStat |= BIT(15);
            if (pointer < 31) {
                coordinateStack[pointer] = coordinate;
                directionStack[pointer]  = direction;
                gxStat += BIT(8);
            }
            break;
        }
        case 3: {
            textureStack = texture;
            break;
        }
    }
    if (--matrixQueue == 0)
        gxStat &= ~BIT(14);
}

void Gpu3D::mtxPopCmd(uint32_t param) {
    switch (matrixMode) {
        case 0: {
            if (gxStat & BIT(13)) {
                gxStat &= ~BIT(13);
                projection = projectionStack;
                clipDirty  = true;
            } else {
                gxStat |= BIT(15);
            }
            break;
        }
        case 1:
        case 2: {
            uint8_t pointer = ((gxStat >> 8) & 0x1F) - ((int8_t)(param << 2) >> 2);
            if (pointer >= 30)
                gxStat |= BIT(15);
            if (pointer < 31) {
                gxStat     = (gxStat & ~0x1F00) | (pointer << 8);
                coordinate = coordinateStack[pointer];
                direction  = directionStack[pointer];
                clipDirty  = true;
            }
            break;
        }
        case 3: {
            texture = textureStack;
            break;
        }
    }
    if (--matrixQueue == 0)
        gxStat &= ~BIT(14);
}

void Gpu3D::mtxStoreCmd(uint32_t param) {
    switch (matrixMode) {
        case 0: {
            projectionStack = projection;
            break;
        }
        case 1:
        case 2: {
            int address = param & 0x0000001F;
            if (address == 31)
                gxStat |= BIT(15);
            coordinateStack[address] = coordinate;
            directionStack[address]  = direction;
            break;
        }
        case 3: {
            textureStack = texture;
            break;
        }
    }
}

void Gpu3D::mtxRestoreCmd(uint32_t param) {
    switch (matrixMode) {
        case 0: {
            projection = projectionStack;
            clipDirty  = true;
            break;
        }
        case 1:
        case 2: {
            int address = param & 0x0000001F;
            if (address == 31)
                gxStat |= BIT(15);
            coordinate = coordinateStack[address];
            direction  = directionStack[address];
            clipDirty  = true;
            break;
        }
        case 3: {
            texture = textureStack;
            break;
        }
    }
}

void Gpu3D::mtxIdentityCmd() {
    switch (matrixMode) {
        case 0: {
            projection = Matrix();
            clipDirty  = true;
            break;
        }
        case 1: {
            coordinate = Matrix();
            clipDirty  = true;
            break;
        }
        case 2: {
            coordinate = Matrix();
            direction  = Matrix();
            clipDirty  = true;
            break;
        }
        case 3: {
            texture = Matrix();
            break;
        }
    }
}

void Gpu3D::mtxLoad44Cmd(std::vector<uint32_t> &params) {
    Matrix matrix = *(Matrix *)&params[0];
    switch (matrixMode) {
        case 0: {
            projection = matrix;
            clipDirty  = true;
            break;
        }
        case 1: {
            coordinate = matrix;
            clipDirty  = true;
            break;
        }
        case 2: {
            coordinate = matrix;
            direction  = matrix;
            clipDirty  = true;
            break;
        }
        case 3: {
            texture = matrix;
            break;
        }
    }
}

void Gpu3D::mtxLoad43Cmd(std::vector<uint32_t> &params) {
    Matrix matrix;
    for (int i = 0; i < 4; i++)
        memcpy(&matrix.data[i * 4], &params[i * 3], 3 * sizeof(int32_t));
    switch (matrixMode) {
        case 0: {
            projection = matrix;
            clipDirty  = true;
            break;
        }
        case 1: {
            coordinate = matrix;
            clipDirty  = true;
            break;
        }
        case 2: {
            coordinate = matrix;
            direction  = matrix;
            clipDirty  = true;
            break;
        }
        case 3: {
            texture = matrix;
            break;
        }
    }
}

void Gpu3D::mtxMult44Cmd(std::vector<uint32_t> &params) {
    Matrix matrix = *(Matrix *)&params[0];
    switch (matrixMode) {
        case 0: {
            projection = matrix * projection;
            clipDirty  = true;
            break;
        }
        case 1: {
            coordinate = matrix * coordinate;
            clipDirty  = true;
            break;
        }
        case 2: {
            coordinate = matrix * coordinate;
            direction  = matrix * direction;
            clipDirty  = true;
            break;
        }
        case 3: {
            texture = matrix * texture;
            break;
        }
    }
}

void Gpu3D::mtxMult43Cmd(std::vector<uint32_t> &params) {
    Matrix matrix;
    for (int i = 0; i < 4; i++)
        memcpy(&matrix.data[i * 4], &params[i * 3], 3 * sizeof(int32_t));
    switch (matrixMode) {
        case 0: {
            projection = matrix * projection;
            clipDirty  = true;
            break;
        }
        case 1: {
            coordinate = matrix * coordinate;
            clipDirty  = true;
            break;
        }
        case 2: {
            coordinate = matrix * coordinate;
            direction  = matrix * direction;
            clipDirty  = true;
            break;
        }
        case 3: {
            texture = matrix * texture;
            break;
        }
    }
}

void Gpu3D::mtxMult33Cmd(std::vector<uint32_t> &params) {
    Matrix matrix;
    for (int i = 0; i < 3; i++)
        memcpy(&matrix.data[i * 4], &params[i * 3], 3 * sizeof(int32_t));
    switch (matrixMode) {
        case 0: {
            projection = matrix * projection;
            clipDirty  = true;
            break;
        }
        case 1: {
            coordinate = matrix * coordinate;
            clipDirty  = true;
            break;
        }
        case 2: {
            coordinate = matrix * coordinate;
            direction  = matrix * direction;
            clipDirty  = true;
            break;
        }
        case 3: {
            texture = matrix * texture;
            break;
        }
    }
}

void Gpu3D::mtxScaleCmd(std::vector<uint32_t> &params) {
    Matrix matrix;
    for (int i = 0; i < 3; i++)
        matrix.data[i * 5] = (int32_t)params[i];
    switch (matrixMode) {
        case 0: {
            projection = matrix * projection;
            clipDirty  = true;
            break;
        }
        case 1:
        case 2: {
            coordinate = matrix * coordinate;
            clipDirty  = true;
            break;
        }
        case 3: {
            texture = matrix * texture;
            break;
        }
    }
}

void Gpu3D::mtxTransCmd(std::vector<uint32_t> &params) {
    Matrix matrix;
    memcpy(&matrix.data[12], &params[0], 3 * sizeof(int32_t));
    switch (matrixMode) {
        case 0: {
            projection = matrix * projection;
            clipDirty  = true;
            break;
        }
        case 1: {
            coordinate = matrix * coordinate;
            clipDirty  = true;
            break;
        }
        case 2: {
            coordinate = matrix * coordinate;
            direction  = matrix * direction;
            clipDirty  = true;
            break;
        }
        case 3: {
            texture = matrix * texture;
            break;
        }
    }
}

void Gpu3D::colorCmd(uint32_t param) {
    savedVertex.color = rgb5ToRgb6(param);
}

void Gpu3D::normalCmd(uint32_t param) {
    Vertex normalVector;
    normalVector.x = ((int16_t)((param & 0x000003FF) << 6)) >> 3;
    normalVector.y = ((int16_t)((param & 0x000FFC00) >> 4)) >> 3;
    normalVector.z = ((int16_t)((param & 0x3FF00000) >> 14)) >> 3;
    if (textureCoordMode == 2) {
        Vertex vertex   = normalVector;
        vertex.w        = 1 << 12;
        Matrix matrix   = texture;
        matrix.data[12] = s << 12;
        matrix.data[13] = t << 12;
        vertex          = vertex * matrix;
        savedVertex.s   = vertex.x >> 12;
        savedVertex.t   = vertex.y >> 12;
    }
    normalVector      = normalVector * direction;
    savedVertex.color = emissionColor;
    for (int i = 0; i < 4; i++) {
        if (enabledLights & BIT(i)) {
            int diffuseLevel = -(lightVector[i] * normalVector);
            if (diffuseLevel < (0 << 12))
                diffuseLevel = (0 << 12);
            if (diffuseLevel > (1 << 12))
                diffuseLevel = (1 << 12);
            int shininessLevel = -(halfVector[i] * normalVector);
            if (shininessLevel < (0 << 12))
                shininessLevel = (0 << 12);
            if (shininessLevel > (1 << 12))
                shininessLevel = (1 << 12);
            shininessLevel = (shininessLevel * shininessLevel) >> 12;
            if (shininessEnabled)
                shininessLevel = shininess[shininessLevel >> 5] << 4;
            int r = (savedVertex.color >> 0) & 0x3F;
            int g = (savedVertex.color >> 6) & 0x3F;
            int b = (savedVertex.color >> 12) & 0x3F;
            r += (((specularColor >> 0) & 0x3F) * ((lightColor[i] >> 0) & 0x3F) * shininessLevel) >> 18;
            g += (((specularColor >> 6) & 0x3F) * ((lightColor[i] >> 6) & 0x3F) * shininessLevel) >> 18;
            b += (((specularColor >> 12) & 0x3F) * ((lightColor[i] >> 12) & 0x3F) * shininessLevel) >> 18;
            r += (((diffuseColor >> 0) & 0x3F) * ((lightColor[i] >> 0) & 0x3F) * diffuseLevel) >> 18;
            g += (((diffuseColor >> 6) & 0x3F) * ((lightColor[i] >> 6) & 0x3F) * diffuseLevel) >> 18;
            b += (((diffuseColor >> 12) & 0x3F) * ((lightColor[i] >> 12) & 0x3F) * diffuseLevel) >> 18;
            r += ((ambientColor >> 0) & 0x3F) * ((lightColor[i] >> 0) & 0x3F) >> 6;
            g += ((ambientColor >> 6) & 0x3F) * ((lightColor[i] >> 6) & 0x3F) >> 6;
            b += ((ambientColor >> 12) & 0x3F) * ((lightColor[i] >> 12) & 0x3F) >> 6;
            if (r < 0)
                r = 0;
            if (r > 0x3F)
                r = 0x3F;
            if (g < 0)
                g = 0;
            if (g > 0x3F)
                g = 0x3F;
            if (b < 0)
                b = 0;
            if (b > 0x3F)
                b = 0x3F;
            savedVertex.color = (b << 12) | (g << 6) | r;
        }
    }
}

void Gpu3D::texCoordCmd(uint32_t param) {
    s = (int16_t)(param >> 0);
    t = (int16_t)(param >> 16);
    if (textureCoordMode == 1) {
        Vertex vertex;
        vertex.x      = s << 8;
        vertex.y      = t << 8;
        vertex.z      = 1 << 8;
        vertex.w      = 1 << 8;
        vertex        = vertex * texture;
        savedVertex.s = vertex.x >> 8;
        savedVertex.t = vertex.y >> 8;
    } else {
        savedVertex.s = s;
        savedVertex.t = t;
    }
}

void Gpu3D::vtx16Cmd(std::vector<uint32_t> &params) {
    savedVertex.x = (int16_t)(params[0] >> 0);
    savedVertex.y = (int16_t)(params[0] >> 16);
    savedVertex.z = (int16_t)(params[1]);
    addVertex();
}

void Gpu3D::vtx10Cmd(uint32_t param) {
    savedVertex.x = (int16_t)((param & 0x000003FF) << 6);
    savedVertex.y = (int16_t)((param & 0x000FFC00) >> 4);
    savedVertex.z = (int16_t)((param & 0x3FF00000) >> 14);
    addVertex();
}

void Gpu3D::vtxXYCmd(uint32_t param) {
    savedVertex.x = (int16_t)(param >> 0);
    savedVertex.y = (int16_t)(param >> 16);
    addVertex();
}

void Gpu3D::vtxXZCmd(uint32_t param) {
    savedVertex.x = (int16_t)(param >> 0);
    savedVertex.z = (int16_t)(param >> 16);
    addVertex();
}

void Gpu3D::vtxYZCmd(uint32_t param) {
    savedVertex.y = (int16_t)(param >> 0);
    savedVertex.z = (int16_t)(param >> 16);
    addVertex();
}

void Gpu3D::vtxDiffCmd(uint32_t param) {
    savedVertex.x += ((int16_t)((param & 0x000003FF) << 6) / 8) >> 3;
    savedVertex.y += ((int16_t)((param & 0x000FFC00) >> 4) / 8) >> 3;
    savedVertex.z += ((int16_t)((param & 0x3FF00000) >> 14) / 8) >> 3;
    addVertex();
}

void Gpu3D::polygonAttrCmd(uint32_t param) {
    polygonAttr = param;
}

void Gpu3D::texImageParamCmd(uint32_t param) {
    savedPolygon.textureAddr  = (param & 0x0000FFFF) << 3;
    savedPolygon.sizeS        = 8 << ((param & 0x00700000) >> 20);
    savedPolygon.sizeT        = 8 << ((param & 0x03800000) >> 23);
    savedPolygon.repeatS      = param & BIT(16);
    savedPolygon.repeatT      = param & BIT(17);
    savedPolygon.flipS        = param & BIT(18);
    savedPolygon.flipT        = param & BIT(19);
    savedPolygon.textureFmt   = (param & 0x1C000000) >> 26;
    savedPolygon.transparent0 = param & BIT(29);
    textureCoordMode          = (param & 0xC0000000) >> 30;
}

void Gpu3D::plttBaseCmd(uint32_t param) {
    savedPolygon.paletteAddr = param & 0x00001FFF;
}

void Gpu3D::difAmbCmd(uint32_t param) {
    diffuseColor = rgb5ToRgb6(param >> 0);
    ambientColor = rgb5ToRgb6(param >> 16);
    if (param & BIT(15))
        savedVertex.color = diffuseColor;
}

void Gpu3D::speEmiCmd(uint32_t param) {
    specularColor    = rgb5ToRgb6(param >> 0);
    emissionColor    = rgb5ToRgb6(param >> 16);
    shininessEnabled = param & BIT(15);
}

void Gpu3D::lightVectorCmd(uint32_t param) {
    lightVector[param >> 30].x = ((int16_t)((param & 0x000003FF) << 6)) >> 3;
    lightVector[param >> 30].y = ((int16_t)((param & 0x000FFC00) >> 4)) >> 3;
    lightVector[param >> 30].z = ((int16_t)((param & 0x3FF00000) >> 14)) >> 3;
    lightVector[param >> 30]   = lightVector[param >> 30] * direction;
    halfVector[param >> 30].x  = (lightVector[param >> 30].x) / 2;
    halfVector[param >> 30].y  = (lightVector[param >> 30].y) / 2;
    halfVector[param >> 30].z  = (lightVector[param >> 30].z - (1 << 12)) / 2;
}

void Gpu3D::lightColorCmd(uint32_t param) {
    lightColor[param >> 30] = rgb5ToRgb6(param);
}

void Gpu3D::shininessCmd(std::vector<uint32_t> &params) {
    for (int i = 0; i < 32; i++) {
        shininess[i * 4 + 0] = params[i] >> 0;
        shininess[i * 4 + 1] = params[i] >> 8;
        shininess[i * 4 + 2] = params[i] >> 16;
        shininess[i * 4 + 3] = params[i] >> 24;
    }
}

void Gpu3D::beginVtxsCmd(uint32_t param) {
    if (vertexCount < 3 + (polygonType & 1))
        vertexCountIn -= vertexCount;
    polygonType                 = param & 0x00000003;
    vertexCount                 = 0;
    clockwise                   = false;
    enabledLights               = polygonAttr & 0x0000000F;
    savedPolygon.mode           = (polygonAttr & 0x00000030) >> 4;
    renderBack                  = polygonAttr & BIT(6);
    renderFront                 = polygonAttr & BIT(7);
    savedPolygon.transNewDepth  = polygonAttr & BIT(11);
    savedPolygon.depthTestEqual = polygonAttr & BIT(14);
    savedPolygon.fog            = polygonAttr & BIT(15);
    savedPolygon.alpha          = ((polygonAttr & 0x001F0000) >> 16) * 2;
    if (savedPolygon.alpha > 0)
        savedPolygon.alpha++;
    savedPolygon.id = (polygonAttr & 0x3F000000) >> 24;
}

void Gpu3D::swapBuffersCmd(uint32_t param) {
    savedPolygon.wBuffer = param & BIT(1);
    state                = GX_HALTED;
}

void Gpu3D::viewportCmd(uint32_t param) {
    viewportX      = ((param & 0x000000FF) >> 0) & 0x1FF;
    viewportY      = (191 - ((param & 0xFF000000) >> 24)) & 0xFF;
    viewportWidth  = (((param & 0x00FF0000) >> 16) - viewportX + 1) & 0x1FF;
    viewportHeight = ((191 - ((param & 0x0000FF00) >> 8)) - viewportY + 1) & 0xFF;
}

void Gpu3D::boxTestCmd(std::vector<uint32_t> &params) {
    int16_t boxTestCoords[6] = {(int16_t)params[0],         (int16_t)(params[0] >> 16), (int16_t)params[1],
                                (int16_t)(params[1] >> 16), (int16_t)params[2],         (int16_t)(params[2] >> 16)};
    boxTestCoords[3] += boxTestCoords[0];
    boxTestCoords[4] += boxTestCoords[1];
    boxTestCoords[5] += boxTestCoords[2];
    static const uint8_t indices[8 * 3] = {0, 1, 2, 3, 1, 2, 0, 4, 2, 0, 1, 5, 3, 4, 2, 3, 1, 5, 0, 4, 5, 3, 4, 5};
    if (clipDirty) {
        clip      = coordinate * projection;
        clipDirty = false;
    }
    Vertex vertices[8];
    for (int i = 0; i < 8; i++) {
        vertices[i].x = boxTestCoords[indices[i * 3 + 0]];
        vertices[i].y = boxTestCoords[indices[i * 3 + 1]];
        vertices[i].z = boxTestCoords[indices[i * 3 + 2]];
        vertices[i].w = 1 << 12;
        vertices[i]   = vertices[i] * clip;
    }
    Vertex faces[6][4] = { {vertices[0], vertices[1], vertices[4], vertices[2]}, {vertices[3], vertices[5], vertices[7], vertices[6]}, {vertices[3], vertices[5], vertices[1], vertices[0]}, {vertices[6], vertices[7], vertices[4], vertices[2]}, {vertices[0], vertices[3], vertices[6], vertices[2]}, {vertices[1], vertices[5], vertices[7], vertices[4]}};
    if ((testQueue -= 3) == 0)
        gxStat &= ~BIT(0);
    for (int i = 0; i < 6; i++) {
        int    size = 4;
        Vertex clipped[10];
        clipPolygon(faces[i], clipped, &size);
        if (size > 0) {
            gxStat |= BIT(1);
            return;
        }
    }
    gxStat &= ~BIT(1);
}

void Gpu3D::posTestCmd(std::vector<uint32_t> &params) {
    savedVertex.x = (int16_t)(params[0] >> 0);
    savedVertex.y = (int16_t)(params[0] >> 16);
    savedVertex.z = (int16_t)(params[1]);
    savedVertex.w = 1 << 12;
    if (clipDirty) {
        clip      = coordinate * projection;
        clipDirty = false;
    }
    Vertex vertex = savedVertex * clip;
    posResult[0]  = vertex.x;
    posResult[1]  = vertex.y;
    posResult[2]  = vertex.z;
    posResult[3]  = vertex.w;
    if ((testQueue -= 2) == 0)
        gxStat &= ~BIT(0);
}

void Gpu3D::vecTestCmd(uint32_t param) {
    Vertex vector;
    vector.x     = ((int16_t)((param & 0x000003FF) << 6)) >> 3;
    vector.y     = ((int16_t)((param & 0x000FFC00) >> 4)) >> 3;
    vector.z     = ((int16_t)((param & 0x3FF00000) >> 14)) >> 3;
    vector       = vector * direction;
    vecResult[0] = ((int16_t)(vector.x << 3)) >> 3;
    vecResult[1] = ((int16_t)(vector.y << 3)) >> 3;
    vecResult[2] = ((int16_t)(vector.z << 3)) >> 3;
    if (--testQueue == 0)
        gxStat &= ~BIT(0);
}

void Gpu3D::addEntry(Entry entry) {
    if (fifo.size() - pipeSize == 0 && pipeSize < 4) {
        fifo.push(entry);
        pipeSize++;
        gxStat |= BIT(27);
    } else {
        if (fifo.size() - pipeSize >= 256)
            core->interpreter[0].halt(1);
        fifo.push(entry);
        gxStat = (gxStat & ~0x01FF0000) | ((fifo.size() - pipeSize) << 16);
        gxStat &= ~BIT(26);
        if (fifo.size() - pipeSize >= 128 && (gxStat & BIT(25)))
            gxStat &= ~BIT(25);
    }
    switch (entry.command) {
        case 0x11:
        case 0x12:
            matrixQueue++;
            gxStat |= BIT(14);
            break;
        case 0x70:
        case 0x71:
        case 0x72:
            testQueue++;
            gxStat |= BIT(0);
            break;
    }
    if (state == GX_IDLE && fifo.size() >= paramCounts[fifo.front().command]) {
        core->schedule(Task(&runCommandTask, 2));
        state = GX_RUNNING;
    }
}

void Gpu3D::writeGxFifo(uint32_t mask, uint32_t value) {
    if (gxFifo == 0) {
        gxFifo = value & mask;
    } else {
        Entry entry(gxFifo, value & mask);
        addEntry(entry);
        gxFifoCount++;
        if (gxFifoCount == paramCounts[gxFifo & 0xFF]) {
            gxFifo >>= 8;
            gxFifoCount = 0;
        }
    }
    while (gxFifo != 0 && paramCounts[gxFifo & 0xFF] == 0) {
        Entry entry(gxFifo, 0);
        addEntry(entry);
        gxFifo >>= 8;
    }
}

void Gpu3D::writeMtxMode(uint32_t mask, uint32_t value) {
    Entry entry(0x10, value & mask);
    addEntry(entry);
}

void Gpu3D::writeMtxPush(uint32_t mask, uint32_t value) {
    Entry entry(0x11, value & mask);
    addEntry(entry);
}

void Gpu3D::writeMtxPop(uint32_t mask, uint32_t value) {
    Entry entry(0x12, value & mask);
    addEntry(entry);
}

void Gpu3D::writeMtxStore(uint32_t mask, uint32_t value) {
    Entry entry(0x13, value & mask);
    addEntry(entry);
}

void Gpu3D::writeMtxRestore(uint32_t mask, uint32_t value) {
    Entry entry(0x14, value & mask);
    addEntry(entry);
}

void Gpu3D::writeMtxIdentity(uint32_t mask, uint32_t value) {
    Entry entry(0x15, value & mask);
    addEntry(entry);
}

void Gpu3D::writeMtxLoad44(uint32_t mask, uint32_t value) {
    Entry entry(0x16, value & mask);
    addEntry(entry);
}

void Gpu3D::writeMtxLoad43(uint32_t mask, uint32_t value) {
    Entry entry(0x17, value & mask);
    addEntry(entry);
}

void Gpu3D::writeMtxMult44(uint32_t mask, uint32_t value) {
    Entry entry(0x18, value & mask);
    addEntry(entry);
}

void Gpu3D::writeMtxMult43(uint32_t mask, uint32_t value) {
    Entry entry(0x19, value & mask);
    addEntry(entry);
}

void Gpu3D::writeMtxMult33(uint32_t mask, uint32_t value) {
    Entry entry(0x1A, value & mask);
    addEntry(entry);
}

void Gpu3D::writeMtxScale(uint32_t mask, uint32_t value) {
    Entry entry(0x1B, value & mask);
    addEntry(entry);
}

void Gpu3D::writeMtxTrans(uint32_t mask, uint32_t value) {
    Entry entry(0x1C, value & mask);
    addEntry(entry);
}

void Gpu3D::writeColor(uint32_t mask, uint32_t value) {
    Entry entry(0x20, value & mask);
    addEntry(entry);
}

void Gpu3D::writeNormal(uint32_t mask, uint32_t value) {
    Entry entry(0x21, value & mask);
    addEntry(entry);
}

void Gpu3D::writeTexCoord(uint32_t mask, uint32_t value) {
    Entry entry(0x22, value & mask);
    addEntry(entry);
}

void Gpu3D::writeVtx16(uint32_t mask, uint32_t value) {
    Entry entry(0x23, value & mask);
    addEntry(entry);
}

void Gpu3D::writeVtx10(uint32_t mask, uint32_t value) {
    Entry entry(0x24, value & mask);
    addEntry(entry);
}

void Gpu3D::writeVtxXY(uint32_t mask, uint32_t value) {
    Entry entry(0x25, value & mask);
    addEntry(entry);
}

void Gpu3D::writeVtxXZ(uint32_t mask, uint32_t value) {
    Entry entry(0x26, value & mask);
    addEntry(entry);
}

void Gpu3D::writeVtxYZ(uint32_t mask, uint32_t value) {
    Entry entry(0x27, value & mask);
    addEntry(entry);
}

void Gpu3D::writeVtxDiff(uint32_t mask, uint32_t value) {
    Entry entry(0x28, value & mask);
    addEntry(entry);
}

void Gpu3D::writePolygonAttr(uint32_t mask, uint32_t value) {
    Entry entry(0x29, value & mask);
    addEntry(entry);
}

void Gpu3D::writeTexImageParam(uint32_t mask, uint32_t value) {
    Entry entry(0x2A, value & mask);
    addEntry(entry);
}

void Gpu3D::writePlttBase(uint32_t mask, uint32_t value) {
    Entry entry(0x2B, value & mask);
    addEntry(entry);
}

void Gpu3D::writeDifAmb(uint32_t mask, uint32_t value) {
    Entry entry(0x30, value & mask);
    addEntry(entry);
}

void Gpu3D::writeSpeEmi(uint32_t mask, uint32_t value) {
    Entry entry(0x31, value & mask);
    addEntry(entry);
}

void Gpu3D::writeLightVector(uint32_t mask, uint32_t value) {
    Entry entry(0x32, value & mask);
    addEntry(entry);
}

void Gpu3D::writeLightColor(uint32_t mask, uint32_t value) {
    Entry entry(0x33, value & mask);
    addEntry(entry);
}

void Gpu3D::writeShininess(uint32_t mask, uint32_t value) {
    Entry entry(0x34, value & mask);
    addEntry(entry);
}

void Gpu3D::writeBeginVtxs(uint32_t mask, uint32_t value) {
    Entry entry(0x40, value & mask);
    addEntry(entry);
}

void Gpu3D::writeEndVtxs(uint32_t mask, uint32_t value) {
    Entry entry(0x41, value & mask);
    addEntry(entry);
}

void Gpu3D::writeSwapBuffers(uint32_t mask, uint32_t value) {
    Entry entry(0x50, value & mask);
    addEntry(entry);
}

void Gpu3D::writeViewport(uint32_t mask, uint32_t value) {
    Entry entry(0x60, value & mask);
    addEntry(entry);
}

void Gpu3D::writeBoxTest(uint32_t mask, uint32_t value) {
    Entry entry(0x70, value & mask);
    addEntry(entry);
}

void Gpu3D::writePosTest(uint32_t mask, uint32_t value) {
    Entry entry(0x71, value & mask);
    addEntry(entry);
}

void Gpu3D::writeVecTest(uint32_t mask, uint32_t value) {
    Entry entry(0x72, value & mask);
    addEntry(entry);
}

void Gpu3D::writeGxStat(uint32_t mask, uint32_t value) {
    if (value & BIT(15))
        gxStat &= ~0xA000;
    mask &= 0xC0000000;
    gxStat = (gxStat & ~mask) | (value & mask);
}

uint32_t Gpu3D::readRamCount() {
    return (vertexCountIn << 16) | polygonCountIn;
}

uint32_t Gpu3D::readClipMtxResult(int index) {
    if (clipDirty) {
        clip      = coordinate * projection;
        clipDirty = false;
    }
    return clip.data[index];
}

uint32_t Gpu3D::readVecMtxResult(int index) {
    return direction.data[(index / 3) * 4 + index % 3];
}

/**
 * @brief Sérialise/désérialise un tableau de 2048 polygones (polygons1 ou polygons2).
 *
 * `_Polygon::vertices` pointe toujours à l'intérieur du tableau de sommets
 * associé (vertices1 pour polygons1, vertices2 pour polygons2 : addPolygon()
 * assigne toujours `vertices` à l'intérieur de `verticesIn`, et verticesIn et
 * polygonsIn permutent ensemble dans swapBuffers(), donc l'association
 * polygons1<->vertices1 / polygons2<->vertices2 ne change jamais). On ne peut
 * pas sérialiser ce pointeur brut tel quel (il ne serait plus valide après un
 * rechargement dans un autre processus) : on le remplace par un simple index
 * relatif à `base`, recalculé après lecture.
 */
static void ioPolygonArray(StateArchive &archive, _Polygon (&polys)[2048], Vertex (&base)[6144]) {
    for (int i = 0; i < 2048; i++) {
        _Polygon &p       = polys[i];
        int32_t   vOffset = archive.saving ? (p.vertices ? int32_t(p.vertices - base) : -1) : 0;
        archive.io(p); // copie brute de la structure (le pointeur vertices y est inclus mais ignoré)
        archive.io(vOffset);
        if (!archive.saving)
            p.vertices = (vOffset >= 0) ? (base + vOffset) : nullptr;
    }
}

void Gpu3D::ioState(StateArchive &archive) {
    // Le thread de rendu (Gpu3DRenderer) lit les polygones/sommets ci-dessous :
    // il doit être arrêté avant qu'on ne les modifie (chargement) ou qu'un
    // rendu concurrent ne les lise pendant qu'on les recopie (sauvegarde).
    core->gpu3DRenderer.quiesce();

    archive.io(state);
    archive.ioQueue(fifo);
    archive.io(pipeSize);
    archive.io(testQueue);
    archive.io(matrixQueue);
    archive.io(matrixMode);
    archive.io(clipDirty);
    archive.io(projection);
    archive.io(projectionStack);
    archive.io(coordinate);
    archive.io(coordinateStack);
    archive.io(direction);
    archive.io(directionStack);
    archive.io(texture);
    archive.io(textureStack);
    archive.io(clip);
    archive.io(vertices1);
    archive.io(vertices2);

    bool inIsSet1 = (verticesIn == vertices1);
    archive.io(inIsSet1);
    if (!archive.saving) {
        verticesIn  = inIsSet1 ? vertices1 : vertices2;
        verticesOut = inIsSet1 ? vertices2 : vertices1;
        polygonsIn  = inIsSet1 ? polygons1 : polygons2;
        polygonsOut = inIsSet1 ? polygons2 : polygons1;
    }

    archive.io(vertexCountIn);
    archive.io(vertexCountOut);

    ioPolygonArray(archive, polygons1, vertices1);
    ioPolygonArray(archive, polygons2, vertices2);

    archive.io(polygonCountIn);
    archive.io(polygonCountOut);
    archive.io(savedVertex);

    // savedPolygon.vertices pointe toujours dans verticesIn (voir addPolygon()),
    // qui vient d'être restauré ci-dessus.
    {
        int32_t vOffset =
            archive.saving ? (savedPolygon.vertices ? int32_t(savedPolygon.vertices - verticesIn) : -1) : 0;
        archive.io(savedPolygon);
        archive.io(vOffset);
        if (!archive.saving)
            savedPolygon.vertices = (vOffset >= 0) ? (verticesIn + vOffset) : nullptr;
    }

    archive.io(s);
    archive.io(t);
    archive.io(vertexCount);
    archive.io(clockwise);
    archive.io(polygonType);
    archive.io(textureCoordMode);
    archive.io(polygonAttr);
    archive.io(enabledLights);
    archive.io(renderBack);
    archive.io(renderFront);
    archive.io(diffuseColor);
    archive.io(ambientColor);
    archive.io(specularColor);
    archive.io(emissionColor);
    archive.io(shininessEnabled);
    archive.io(lightVector);
    archive.io(halfVector);
    archive.io(lightColor);
    archive.io(shininess);
    archive.io(viewportX);
    archive.io(viewportY);
    archive.io(viewportWidth);
    archive.io(viewportHeight);
    archive.io(gxFifo);
    archive.io(gxStat);
    archive.io(posResult);
    archive.io(vecResult);
    archive.io(gxFifoCount);

    // Le scheduler (Core::tasks) n'est pas sérialisé : si une commande GXFIFO
    // était prête à s'exécuter au moment de la sauvegarde, il faut la
    // reprogrammer, à l'identique de la condition utilisée ailleurs dans ce
    // fichier (cf. runCommand()/swapBuffers()).
    if (!archive.saving && state != GX_HALTED && !fifo.empty() && fifo.size() >= paramCounts[fifo.front().command])
        core->schedule(Task(&runCommandTask, 2));
}

} // namespace emulator_demo

