/*
 * Private DirectComposition renderer contract.
 *
 * This file is not part of the Windows API. It describes committed visual
 * state passed from dcomp through dxgi to the graphics backend.
 */
#ifndef __WINE_PRIVATE_DCOMP_H
#define __WINE_PRIVATE_DCOMP_H

#include "windef.h"

#define WINE_DCOMP_VISUAL_DESC_VERSION 1

#define WINE_DCOMP_VISUAL_HAS_SIZE       0x00000001
#define WINE_DCOMP_VISUAL_HAS_CLIP       0x00000002
#define WINE_DCOMP_VISUAL_BACKFACE_HIDDEN 0x00000004
#define WINE_DCOMP_VISUAL_RENDERER_ACTIVE 0x00000008
#define WINE_DCOMP_VISUAL_TRANSFORM_ABSOLUTE 0x00000010

struct wine_dcomp_visual_desc
{
    UINT version;
    UINT flags;
    /* Row-vector System.Numerics convention. */
    float transform[16];
    /* Target-space origin of the composition window's render target. */
    float render_origin[2];
    float size[2];
    float clip[4];
    float opacity;
    float source_size[2];
    float content_rect[4];
    UINT interpolation_mode;
    UINT border_mode;
    UINT composite_mode;
};

typedef struct IDCompositionVisualPrivate IDCompositionVisualPrivate;
typedef struct IDCompositionVisualPrivateVtbl
{
    HRESULT (WINAPI *QueryInterface)(IDCompositionVisualPrivate *, REFIID, void **);
    ULONG (WINAPI *AddRef)(IDCompositionVisualPrivate *);
    ULONG (WINAPI *Release)(IDCompositionVisualPrivate *);
    HRESULT (WINAPI *SetIsVisible)(IDCompositionVisualPrivate *, BOOL);
    HRESULT (WINAPI *SetDescription)(IDCompositionVisualPrivate *,
            const struct wine_dcomp_visual_desc *);
    HRESULT (WINAPI *GetEffectiveVisibility)(IDCompositionVisualPrivate *, BOOL *);
} IDCompositionVisualPrivateVtbl;

struct IDCompositionVisualPrivate
{
    const IDCompositionVisualPrivateVtbl *lpVtbl;
};

static const GUID IID_IDCompositionVisualPrivate =
    {0xbbe3ed3b, 0xdbb4, 0x4d7c, {0x9c, 0xc2, 0xa6, 0x2c, 0x7d, 0xed, 0xc2, 0x9f}};

#endif /* __WINE_PRIVATE_DCOMP_H */
