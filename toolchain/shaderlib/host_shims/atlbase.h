// A minimal stand-in for ATL's atlbase.h, only for building DXC's host tablegens on Windows
// with clang-cl and an xwin sysroot (which has no ATL). DXC's LLVMSupport/MSSupport use just
// CComPtr, CComHeapPtr and ATLASSERT. The classes follow DXC's own non-Windows versions
// (include/dxc/WinAdapter.h). Never part of anything shipped.
#pragma once

#include <assert.h>
#include <objbase.h>
#include <unknwn.h>
#include <windows.h>

#define ATLASSERT assert

template <class T> class CComPtrBase {
protected:
  CComPtrBase() throw() { p = nullptr; }
  CComPtrBase(T *lp) throw() {
    p = lp;
    if (p != nullptr)
      p->AddRef();
  }
  void Swap(CComPtrBase &other) {
    T *pTemp = p;
    p = other.p;
    other.p = pTemp;
  }

public:
  ~CComPtrBase() throw() {
    if (p) {
      p->Release();
      p = nullptr;
    }
  }
  operator T *() const throw() { return p; }
  T &operator*() const { return *p; }
  T *operator->() const { return p; }
  T **operator&() throw() { return &p; }
  bool operator!() const throw() { return (p == nullptr); }
  bool operator<(T *pT) const throw() { return p < pT; }
  bool operator!=(T *pT) const { return !operator==(pT); }
  bool operator==(T *pT) const throw() { return p == pT; }
  void Release() throw() {
    T *pTemp = p;
    if (pTemp) {
      p = nullptr;
      pTemp->Release();
    }
  }
  void Attach(T *p2) throw() {
    if (p)
      p->Release();
    p = p2;
  }
  T *Detach() throw() {
    T *pt = p;
    p = nullptr;
    return pt;
  }
  HRESULT CopyTo(T **ppT) throw() {
    if (ppT == nullptr)
      return E_POINTER;
    *ppT = p;
    if (p)
      p->AddRef();
    return S_OK;
  }
  template <class Q> HRESULT QueryInterface(Q **pp) const throw() {
    return p->QueryInterface(__uuidof(Q), (void **)pp);
  }
  T *p;
};

template <class T> class CComPtr : public CComPtrBase<T> {
public:
  CComPtr() throw() {}
  CComPtr(T *lp) throw() : CComPtrBase<T>(lp) {}
  CComPtr(const CComPtr<T> &lp) throw() : CComPtrBase<T>(lp.p) {}
  template <typename Q> CComPtr(const CComPtr<Q> &lp) throw() : CComPtrBase<T>(lp.p) {}
  CComPtr(CComPtr<T> &&lp) throw() : CComPtrBase<T>() { lp.Swap(*this); }
  T *operator=(T *lp) throw() {
    if (*this != lp)
      CComPtr(lp).Swap(*this);
    return *this;
  }
  T *operator=(const CComPtr<T> &lp) throw() {
    if (*this != lp)
      CComPtr(lp).Swap(*this);
    return *this;
  }
  T *operator=(CComPtr<T> &&lp) throw() {
    if (*this != lp)
      CComPtr(static_cast<CComPtr &&>(lp)).Swap(*this);
    return *this;
  }
};

class CCRTAllocator {
public:
  static void *Reallocate(void *p, size_t n) throw() { return realloc(p, n); }
  static void *Allocate(size_t n) throw() { return malloc(n); }
  static void Free(void *p) throw() { free(p); }
};

class CComAllocator {
public:
  static void *Reallocate(void *p, size_t n) throw() { return CoTaskMemRealloc(p, n); }
  static void *Allocate(size_t n) throw() { return CoTaskMemAlloc(n); }
  static void Free(void *p) throw() { CoTaskMemFree(p); }
};

template <class T, class Allocator = CCRTAllocator> class CHeapPtrBase {
protected:
  CHeapPtrBase() throw() : m_pData(nullptr) {}
  CHeapPtrBase(CHeapPtrBase<T, Allocator> &p) throw() { m_pData = p.Detach(); }
  explicit CHeapPtrBase(T *pData) throw() : m_pData(pData) {}
  CHeapPtrBase<T, Allocator> &operator=(CHeapPtrBase<T, Allocator> &p) throw() {
    if (m_pData != p.m_pData)
      Attach(p.Detach());
    return *this;
  }

public:
  ~CHeapPtrBase() throw() { Free(); }
  operator T *() const throw() { return m_pData; }
  T *operator->() const throw() { return m_pData; }
  T **operator&() throw() { return &m_pData; }
  bool AllocateBytes(size_t nBytes) throw() {
    m_pData = static_cast<T *>(Allocator::Allocate(nBytes));
    return m_pData != nullptr;
  }
  void Attach(T *pData) throw() {
    Allocator::Free(m_pData);
    m_pData = pData;
  }
  T *Detach() throw() {
    T *pTemp = m_pData;
    m_pData = nullptr;
    return pTemp;
  }
  void Free() throw() {
    Allocator::Free(m_pData);
    m_pData = nullptr;
  }
  bool ReallocateBytes(size_t nBytes) throw() {
    T *pNew = static_cast<T *>(Allocator::Reallocate(m_pData, nBytes));
    if (pNew == nullptr)
      return false;
    m_pData = pNew;
    return true;
  }
  T *m_pData;
};

template <typename T, class Allocator = CCRTAllocator>
class CHeapPtr : public CHeapPtrBase<T, Allocator> {
public:
  CHeapPtr() throw() {}
  CHeapPtr(CHeapPtr<T, Allocator> &p) throw() : CHeapPtrBase<T, Allocator>(p) {}
  explicit CHeapPtr(T *p) throw() : CHeapPtrBase<T, Allocator>(p) {}
  CHeapPtr<T, Allocator> &operator=(CHeapPtr<T, Allocator> &p) throw() {
    CHeapPtrBase<T, Allocator>::operator=(p);
    return *this;
  }
  bool Allocate(size_t nElements = 1) throw() { return this->AllocateBytes(nElements * sizeof(T)); }
  bool Reallocate(size_t nElements) throw() { return this->ReallocateBytes(nElements * sizeof(T)); }
};

template <typename T> class CComHeapPtr : public CHeapPtr<T, CComAllocator> {
public:
  CComHeapPtr() throw() {}
  explicit CComHeapPtr(T *pData) throw() : CHeapPtr<T, CComAllocator>(pData) {}
};
