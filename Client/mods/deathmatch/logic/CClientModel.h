/*****************************************************************************
*
*  PROJECT:     Multi Theft Auto
*               (Shared logic for modifications)
*  LICENSE:     See LICENSE in the top level directory
*  FILE:        mods/deathmatch/logic/CClientModel.h
*  PURPOSE:     Model handling class
*
*****************************************************************************/

class CClientModel;

#pragma once

#include <list>
#include "CClientModelManager.h"

enum class eClientModelType
{
    PED,
    OBJECT,
    VEHICLE,
};

class CClientModel
{
    friend class CClientModelManager;

public:
    CClientModel(CClientManager* pManager, int iModelID, eClientModelType eModelType);
    ~CClientModel(void);

    int                             GetModelID(void) const { return m_iModelID; };
    eClientModelType                GetModelType(void) const { return m_eModelType; };
    bool                            Allocate(ushort usParentID);
    bool                            Deallocate(void);
    void                            SetParentResource(CResource* pResource) { m_pParentResource = pResource; }
    CResource*                      GetParentResource(void) const { return m_pParentResource; }

protected:
    CClientManager* m_pManager;
    class CClientModelManager*      m_pModelManager;

    int                             m_iModelID;
    eClientModelType                m_eModelType;
    bool                            m_bAllocatedByUs;

    CResource*                      m_pParentResource; // Resource that allocated model
};
