//**************************************************************************
//
// SymbolSet.cpp
//
// The implementation for our notion of a "symbol set".  A "symbol set" is an
// abstraction for the available symbols for a given module.  It is a set of
// "stacked" interfaces which implements progressively more functionality depending
// on the complexity of the symbol implementation.
//
//**************************************************************************
//
// Copyright (c) Microsoft Corporation.  All rights reserved.
//
//**************************************************************************

#include "SymBuilder.h"

using namespace Microsoft::WRL;

namespace Debugger
{
namespace TargetComposition
{
namespace Services
{
namespace SymbolBuilder
{

//*************************************************
// General Symbol Set:
//

bool SymbolRangeList::FindSymbols(_In_ ULONG64 address, _Out_ SymbolList const** pSymbolList)
{
    auto it = std::lower_bound(m_ranges.begin(), m_ranges.end(), address,
                               [&](_In_ const AddressRange& rng, _In_ ULONG64 address)
                               {
                                   return rng.End < address;
                               });

    if (it == m_ranges.end() || it->Start > address)
    {
        return false;
    }

    *pSymbolList = &(it->Symbols);
    return true;
}

HRESULT SymbolRangeList::AddSymbol(_In_ ULONG64 start, _In_ ULONG64 end, _In_ ULONG64 symbol)
{
    //
    // We cannot let a C++ exception escape.
    //
    auto fn = [&]()
    {
        //
        // We must find the proper position within the address range to place the symbol in sorted
        // order.  If there is no overlap, this is easy.  If there is overlap, we must split the ranges
        // as appropriate.
        //
        auto it = std::lower_bound(m_ranges.begin(), m_ranges.end(), start,
                                   [&](_In_ const AddressRange& rng, _In_ ULONG64 address)
                                   {
                                       return rng.End < address;
                                   });

        //
        // If there are no address ranges which begin above 'start', just append the range and be done.
        // Likewise, if there is *AND* there is no overlap with any other known address range, just insert
        // the new range before the one which is greater.
        //
        if (it == m_ranges.end())
        {
            m_ranges.push_back( { start, end, { symbol } } );
            return S_OK;
        }

        if (end <= it->Start)
        {
            m_ranges.insert(it, { start, end, { symbol } } );
            return S_OK;
        }

        //
        // At this point, there is at least some overlap.  Walk forward, adding symbol to the ranges where
        // appropriate and splitting where not.
        //
        size_t cur = it - m_ranges.begin();
        while (start < end)
        {
            //
            // If we have gotten to the point where there's nothing left and we still have range, append it.
            //
            if (cur >= m_ranges.size())
            {
                m_ranges.push_back( { start, end, { symbol } });
                start = end;
                break;
            }

            AddressRange& rng = m_ranges[cur];
            
            //
            // If there is 100% overlap, just add the new symbol and be done.
            //
            if (start == rng.Start && end == rng.End)
            {
                rng.Symbols.push_back(symbol);
                start = end;
                break;
            }

            //
            // If there is a part of the range before rng, insert it and continue on.
            //
            if (start < rng.Start)
            {
                ULONG64 curEnd = end;
                if (curEnd > rng.Start)
                {
                    curEnd = rng.Start;
                }

                if (curEnd - start > 0)
                {
                    m_ranges.insert(m_ranges.begin() + cur, { start, curEnd, { symbol } });
                    ++cur;
                    start = curEnd;
                    continue;
                }
            }

            if (start >= rng.Start && start < rng.End)
            {
                //
                // There is overlap.  Start is within the range.  End may be within the range *OR* it may be
                // outside of the range.  We need to split the range.  There are three split points:
                //
                // 1: [rng.Start, start)
                // 2: [start, end)
                // 3: [end, rng.End)
                //
                // Note that if end goes outside the bounds of rng, there are only two:
                //
                // 1: [rng.Start, start)
                // 2: [start, rng.End)
                //
                // But in the latter case, we will have to loop back up and continue with whatever was outside
                // the bounds of rng.
                //
                if (start > rng.Start)
                {
                    auto itn = m_ranges.insert(m_ranges.begin() + cur, { rng.Start, start, rng.Symbols } );
                    ++cur;
                    AddressRange& existingRange = m_ranges[cur];
                    existingRange.Start = start;
                    continue;
                }
                else if (end >= rng.End)
                {
                    rng.Symbols.push_back(symbol);
                    start = rng.End;
                    ++cur;
                    continue;
                }
                else
                {
                    //
                    // We need to split again -- this is that #2 range in the first case above.  First, add
                    // the new range for #3
                    //
                    auto itb = m_ranges.begin() + cur + 1;
                    m_ranges.insert(itb, { end, rng.End, rng.Symbols });
                    AddressRange& existingRange = m_ranges[cur];
                    existingRange.End = end;
                    existingRange.Symbols.push_back(symbol);
                    start = end;
                    break;
                }
            }
            else
            {
                //
                // It's not this range.  Move onto the next one.
                //
                ++cur;
                continue;
            }
        }

        return S_OK;
    };
    return ConvertException(fn);
}

HRESULT SymbolRangeList::RemoveSymbol(_In_ ULONG64 start, _In_ ULONG64 end, _In_ ULONG64 symbol)
{
    //
    // We cannot let a C++ exception escape.
    //
    auto fn = [&]()
    {
        //
        // Find where the first range associated with this symbol is and remove everything between
        // [start, end)
        //
        auto it = std::lower_bound(m_ranges.begin(), m_ranges.end(), start,
                                   [&](_In_ const AddressRange& rng, _In_ ULONG64 start)
                                   {
                                       return rng.End < start;
                                   });

        if (it == m_ranges.end() || it->Start > end)
        {
            //
            // We could not find this range.  It's not a failure per-se.  Just return S_FALSE to the caller
            // to let them know nothing was actually removed!
            //
            return S_FALSE;
        }

        //
        // We need to keep walking and removing symbols or splitting ranges until we hit a range which is
        // past the end of the sought half-open set [start, end)
        //
        size_t cur = it - m_ranges.begin();
        while (cur < m_ranges.size() && end > m_ranges[cur].Start)
        {
            AddressRange& rng = m_ranges[cur];

            //
            // If the range is equivalent or a sub-range, just remove the symbol
            //
            if (start <= rng.Start && end >= rng.End)
            {
                RemoveSymbolFromList(rng.Symbols, symbol);
                ++cur;
                continue;
            }

            //
            // If there is a sub-portion at the beginning of the range that we are *NOT* removing the
            // symbol index from, we need to split.
            //
            // This would be something like:
            //
            //     range:   [              )
            //    remove:         [        )
            //
            // Where we now need:
            //
            //    range1:   [     )                 <-- has symbol
            //    range2:         [        )        <-- does not have symbol
            //
            if (start > rng.Start && start < rng.End)
            {
                auto itb = m_ranges.begin() + cur + 1;
                auto itn = m_ranges.insert(itb, { start, rng.End, rng.Symbols } );      // range2
                RemoveSymbolFromList(itn->Symbols, symbol);

                AddressRange& existingRange = m_ranges[cur];
                existingRange.End = start;
                ++cur;
                continue;
            }

            //
            // Now we need to check the other side of this (the above will fall into this after the split and continue)
            //
            // We may need a further split having something like:
            //
            //     range:   [              )
            //    remove:   [        )
            //
            // Where we now need:
            //
            //    range1:   [        )              <-- does not have symbol
            //    range2:            [     )        <-- has symbol
            //
            else if (end > rng.Start && end < rng.End)
            {
                auto itb = m_ranges.begin() + cur + 1;
                auto itn = m_ranges.insert(itb, { end, rng.End, rng.Symbols });      // range2

                AddressRange& existingRange = m_ranges[cur];
                existingRange.End = end;
                RemoveSymbolFromList(existingRange.Symbols, symbol);
                ++cur;
                continue;
            }

            break;
        }

        return S_OK;
    };
    return ConvertException(fn);
}

IDebugServiceManager* SymbolSet::GetServiceManager() const
{
    return m_pOwningProcess->GetServiceManager();
}

ISvcMachineArchitecture* SymbolSet::GetArchInfo() const
{
    return m_pOwningProcess->GetArchInfo();
}

HRESULT SymbolSet::AddBasicCTypes()
{
    HRESULT hr = S_OK;

    ComPtr<BasicTypeSymbol> spNew;
    IfFailedReturn(MakeAndInitialize<BasicTypeSymbol>(&spNew, this, SvcSymbolIntrinsicVoid, 0, L"void"));
    IfFailedReturn(MakeAndInitialize<BasicTypeSymbol>(&spNew, this, SvcSymbolIntrinsicBool, 1, L"bool"));
    IfFailedReturn(MakeAndInitialize<BasicTypeSymbol>(&spNew, this, SvcSymbolIntrinsicChar, 1, L"char"));
    IfFailedReturn(MakeAndInitialize<BasicTypeSymbol>(&spNew, this, SvcSymbolIntrinsicUInt, 1, L"unsigned char"));
    IfFailedReturn(MakeAndInitialize<BasicTypeSymbol>(&spNew, this, SvcSymbolIntrinsicWChar, 2, L"wchar_t"));
    IfFailedReturn(MakeAndInitialize<BasicTypeSymbol>(&spNew, this, SvcSymbolIntrinsicInt, 2, L"short"));
    IfFailedReturn(MakeAndInitialize<BasicTypeSymbol>(&spNew, this, SvcSymbolIntrinsicUInt, 2, L"unsigned short"));
    IfFailedReturn(MakeAndInitialize<BasicTypeSymbol>(&spNew, this, SvcSymbolIntrinsicInt, 4, L"int"));
    IfFailedReturn(MakeAndInitialize<BasicTypeSymbol>(&spNew, this, SvcSymbolIntrinsicUInt, 4, L"unsigned int"));
    IfFailedReturn(MakeAndInitialize<BasicTypeSymbol>(&spNew, this, SvcSymbolIntrinsicInt, 8, L"__int64"));
    IfFailedReturn(MakeAndInitialize<BasicTypeSymbol>(&spNew, this, SvcSymbolIntrinsicUInt, 8, L"unsigned __int64"));
    IfFailedReturn(MakeAndInitialize<BasicTypeSymbol>(&spNew, this, SvcSymbolIntrinsicLong, 4, L"long"));
    IfFailedReturn(MakeAndInitialize<BasicTypeSymbol>(&spNew, this, SvcSymbolIntrinsicULong, 4, L"unsigned long"));
    IfFailedReturn(MakeAndInitialize<BasicTypeSymbol>(&spNew, this, SvcSymbolIntrinsicFloat, 4, L"float"));
    IfFailedReturn(MakeAndInitialize<BasicTypeSymbol>(&spNew, this, SvcSymbolIntrinsicFloat, 8, L"double"));

    return hr;
}

HRESULT SymbolSet::AddNewSymbol(_In_ BaseSymbol *pBaseSymbol, _Out_ ULONG64 *pUniqueId)
{
    //
    // We cannot let a C++ exception escape.
    //
    auto fn = [&]()
    {
        ULONG64 uniqueId = GetUniqueId();
        if (m_symbols.size() <= uniqueId + 1)
        {
            m_symbols.resize(static_cast<size_t>(uniqueId + 1));
        }

        m_symbols[uniqueId] = pBaseSymbol;

        if (pBaseSymbol->IsGlobal())
        {
            m_globalSymbols.push_back(uniqueId);
            if (!pBaseSymbol->InternalGetQualifiedName().empty())
            {
                m_symbolNameMap.insert( { pBaseSymbol->InternalGetQualifiedName(), uniqueId });
            }
        }

        *pUniqueId = uniqueId;

        // 
        // Send an advisory notification upwards that everyone should flush caches.  Do not consider this
        // a failure to create the symbol if something goes wrong.  At worst, an explicit .reload will be
        // required in the debugger.
        //
        (void)InvalidateExternalCaches();

        return S_OK;
    };
    return ConvertException(fn);
}

HRESULT SymbolSet::DeleteExistingSymbol(_In_ ULONG64 uniqueId)
{
    //
    // We cannot let a C++ exception escape.
    //
    auto fn = [&]()
    {
        //
        // As we hand out 'uniqueId' based on position within a vector, it should always fit within the
        // bounds of a size_t.  
        //
        if (uniqueId > std::numeric_limits<size_t>::max())
        {
            return E_INVALIDARG;
        }

        BaseSymbol *pSymbol = InternalGetSymbol(uniqueId);
        if (pSymbol != nullptr)
        {
            if (pSymbol->IsGlobal())
            {
                for (auto itg = m_globalSymbols.begin(); itg != m_globalSymbols.end(); ++itg)
                {
                    if (*itg == uniqueId)
                    {
                        m_globalSymbols.erase(itg);
                        break;
                    }
                }

                if (!pSymbol->InternalGetQualifiedName().empty())
                {
                    auto itn = m_symbolNameMap.find(pSymbol->InternalGetQualifiedName());
                    if (itn != m_symbolNameMap.end())
                    {
                        m_symbolNameMap.erase(itn);
                    }
                }
            }

            m_symbols[static_cast<size_t>(uniqueId)] = nullptr;

            //
            // Send an advisory notification upwards that everyone should flush caches.  Do not consider this
            // a failure to delete the symbol if something goes wrong.  At worst, an explicit .reload will be
            // required by the debugger.
            //
            (void)InvalidateExternalCaches();
        }

        return S_OK;
    };
    return ConvertException(fn);
}

HRESULT SymbolSet::EnumerateAllSymbols(_COM_Outptr_ ISvcSymbolSetEnumerator **ppEnumerator)
{
    HRESULT hr = S_OK;
    *ppEnumerator = nullptr;

    ComPtr<GlobalEnumerator> spEnum;
    IfFailedReturn(MakeAndInitialize<GlobalEnumerator>(&spEnum, this));

    *ppEnumerator = spEnum.Detach();
    return hr;
}

HRESULT SymbolSet::InvalidateExternalCaches()
{
    HRESULT hr = S_OK;

    IDebugServiceManager *pServiceManager = GetServiceManager();
    if (pServiceManager == nullptr)
    {
        return E_UNEXPECTED;
    }

    ComPtr<SymbolCacheInvalidateArguments> spArgs;
    IfFailedReturn(MakeAndInitialize<SymbolCacheInvalidateArguments>(&spArgs, m_spModule.Get(), this));

    HRESULT hrEvent;
    IfFailedReturn(pServiceManager->FireEventNotification(DEBUG_SVCEVENT_SYMBOLCACHEINVALIDATE, spArgs.Get(), &hrEvent));

    //
    // While we get a sink result if some handler decided to return a failure from their handling of the event, we
    // are not going to propagate that upwards.  There's not really much we can do in this case.
    //
    return hr;
}

HRESULT SymbolSet::FindTypeByName(_In_ std::wstring const& typeName,
                                  _Out_ ULONG64 *pTypeId,
                                  _Outptr_ BaseTypeSymbol **ppTypeSymbol,
                                  _In_ bool allowAutoCreations)
{
    HRESULT hr = S_OK;

    if (ppTypeSymbol != nullptr)
    {
        *ppTypeSymbol = nullptr;
    }

    //
    // We cannot let a C++ exception escape.
    //
    auto fn = [&]()
    {
        ULONG64 symId = InternalGetSymbolIdByName(typeName);
        BaseSymbol *pSymbol = nullptr;
        if (symId != 0)
        {
            pSymbol = InternalGetSymbol(symId);
            if (pSymbol == nullptr || pSymbol->InternalGetKind() != SvcSymbolType)
            {
                return E_INVALIDARG;
            }

            *pTypeId = symId;

            if (ppTypeSymbol != nullptr)
            {
                *ppTypeSymbol = static_cast<BaseTypeSymbol *>(pSymbol);
            }

            return hr;
        }
        else if (!allowAutoCreations)
        {
            return E_INVALIDARG;
        }

        //
        // Is this a pointer type or something similar which we will allow "on demand" creation of according
        // to standard C like semantics.
        //
        wchar_t const *pStart = typeName.c_str();
        wchar_t const *pc = (pStart + wcslen(pStart) - 1);

        switch(*pc)
        {
            case L'*':
            case L'&':
            case L'^':
            {
                if (!m_demandCreatePointerTypes)
                {
                    return E_FAIL;
                }

                SvcSymbolPointerKind pointerKind = SvcSymbolPointerStandard;
                if (*pc == L'&')
                {
                    if (*(pc - 1) == L'&' && (pc - 1) != pStart)
                    {
                        --pc;
                        pointerKind = SvcSymbolPointerRValueReference;
                    }
                    else
                    {
                        pointerKind = SvcSymbolPointerReference;
                    }
                }
                else if (*pc == L'^')
                {
                    pointerKind = SvcSymbolPointerCXHat;
                }

                wchar_t const *pPrior = (pc - 1);
                while (pPrior > pStart && iswspace(*pPrior)) { --pPrior; }

                if (pPrior == pStart)
                {
                    return E_INVALIDARG;
                }

                std::wstring baseName(pStart, pPrior - pStart + 1);

                ULONG64 pointedToId = 0;
                IfFailedReturn(FindTypeByName(baseName, &pointedToId, nullptr));

                ComPtr<PointerTypeSymbol> spPointerType;
                IfFailedReturn(MakeAndInitialize<PointerTypeSymbol>(&spPointerType, this, pointedToId, pointerKind));

                symId = spPointerType->InternalGetId();

                //
                // NOTE: This is safe to be held past the ComPtr destruction above because creation of the symbol
                //       will add it to our internal management lists...  and nothing could possibly have deleted
                //       it before returning from this method!
                //
                pSymbol = spPointerType.Get();

                break;
            }

            case L']':
            {
                if (!m_demandCreateArrayTypes)
                {
                    return E_FAIL;
                }

                wchar_t const *pb = pc - 1;
                while (pb > pStart && *pb != L'[')
                {
                    --pb;
                }

                if (pb == pStart)
                {
                    return E_INVALIDARG;
                }

                ULONG64 dim = 0;
                wchar_t const *pDig = pb + 1;
                while (*pDig != L']')
                {
                    if (*pDig >= L'0' && *pDig <= L'9')
                    {
                        dim = (dim * 10) + (*pDig - L'0');
                    }
                    else
                    {
                        return E_INVALIDARG;
                    }
                    ++pDig;
                }

                std::wstring baseName(pStart, pb - pStart);

                ULONG64 arrayOfId = 0;
                IfFailedReturn(FindTypeByName(baseName, &arrayOfId, nullptr));

                ComPtr<ArrayTypeSymbol> spArrayType;
                IfFailedReturn(MakeAndInitialize<ArrayTypeSymbol>(&spArrayType, this, arrayOfId, dim));

                symId = spArrayType->InternalGetId();

                //
                // NOTE: This is safe to be held past the ComPtr destruction above because creation of the symbol
                //       will add it to our internal management lists...  and nothing could possibly have deleted
                //       it before returning from this method!
                //
                pSymbol = spArrayType.Get();
                break;
            }

            default:
                break;
        }

        if (symId == 0)
        {
            return E_INVALIDARG;
        }

        if (ppTypeSymbol != nullptr)
        {
            *ppTypeSymbol = static_cast<BaseTypeSymbol *>(pSymbol);
        }

        return hr;
    };
    return ConvertException(fn);
}

HRESULT SymbolSet::FindSymbolByName(_In_ PCWSTR symbolName, _COM_Outptr_ ISvcSymbol **ppSymbol)
{
    *ppSymbol = nullptr;

    //
    // We cannot let a C++ exception escape.
    //
    auto fn = [&]()
    {
        std::wstring name = symbolName;
        auto it = m_symbolNameMap.find(name);
        if (it == m_symbolNameMap.end())
        {
            return E_BOUNDS;
        }

        BaseSymbol *pSymbol = InternalGetSymbol(it->second);
        if (pSymbol == nullptr)
        {
            return E_BOUNDS;
        }

        ComPtr<ISvcSymbol> spSymbol = pSymbol;
        *ppSymbol = spSymbol.Detach();
        return S_OK;
    };
    return ConvertException(fn);
}

HRESULT SymbolSet::FindSymbolByOffset(_In_ ULONG64 moduleOffset,
                                      _In_ bool exactMatchOnly,
                                      _COM_Outptr_ ISvcSymbol **ppSymbol,
                                      _Out_ ULONG64 *pSymbolOffset)
{
    HRESULT hr = S_OK;
    *ppSymbol = nullptr;

    SymbolRangeList::SymbolList const* pSymbols;
    if (!m_symbolRanges.FindSymbols(moduleOffset, &pSymbols) || pSymbols->size() == 0)
    {
        return E_BOUNDS;
    }

    BaseSymbol *pSymbol = InternalGetSymbol((*pSymbols)[0]);

    ULONG64 symbolOffset;
    IfFailedReturn(pSymbol->GetOffset(&symbolOffset));

    if (exactMatchOnly && symbolOffset != moduleOffset)
    {
        return E_BOUNDS;
    }

    ComPtr<ISvcSymbol> spSymbol = pSymbol;

    *pSymbolOffset = (moduleOffset - symbolOffset);
    *ppSymbol = spSymbol.Detach();
    return S_OK;
}

} // SymbolBuilder
} // Services
} // TargetComposition
} // Debugger
