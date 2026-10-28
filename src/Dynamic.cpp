#include "Dynamic.h"
#include "WindowUtils.h"
#include <algorithm>
#include <cmath>
#include <cwctype>

namespace {
using Tree = DynamicTree;
bool HasChildren(const DynamicNode& node) { return node.first >= 0; }
int Extent(const RECT& r, bool vertical) {
    return vertical ? r.right - r.left : r.bottom - r.top;
}
std::pair<RECT, RECT> Divide(RECT r, const DynamicNode& node) {
    const int pixel = static_cast<int>(std::lround(Extent(r,node.vertical) * node.ratio));
    RECT a = r, b = r;
    if (node.vertical) { a.right = r.left + pixel; b.left = a.right; }
    else { a.bottom = r.top + pixel; b.top = a.bottom; }
    return {a,b};
}
void Reflow(const Tree& tree, int index, RECT bounds, int padding) {
    if (index < 0 || index >= static_cast<int>(tree.nodes.size())) return;
    const auto& n = tree.nodes[index];
    if (HasChildren(n)) {
        auto [a,b] = Divide(bounds,n);
        Reflow(tree,n.first,a,padding);
        Reflow(tree,n.second,b,padding);
    } else if (n.window && IsWindow(n.window)) {
        RECT target = bounds;
        InflateRect(&target,-std::min(padding,static_cast<int>((bounds.right-bounds.left-1)/2)),
                              -std::min(padding,static_cast<int>((bounds.bottom-bounds.top-1)/2)));
        if (IsZoomed(n.window)) ShowWindow(n.window,SW_RESTORE);
        SetWindowPos(n.window,nullptr,target.left,target.top,
            std::max(1L,target.right-target.left),std::max(1L,target.bottom-target.top),
            SWP_NOACTIVATE|SWP_NOZORDER);
    }
}
bool Remove(Tree& tree,int& index,HWND window) {
    if (index < 0) return false;
    auto& n=tree.nodes[index];
    if (!HasChildren(n)) {
        if (n.window != window) return false;
        index=-1;
        return true;
    }
    int a=n.first,b=n.second;
    bool removed=Remove(tree,a,window);
    if (!removed) removed=Remove(tree,b,window);
    if (removed) {
        // A removed child causes its sibling to occupy the entire parent region.
        if (a<0) index=b;
        else if (b<0) index=a;
        else { n.first=a; n.second=b; }
    }
    return removed;
}
struct Hit { int index{-1}; RECT rect{}; };
Hit FindLeaf(const Tree& tree,int index,RECT bounds,POINT point) {
    if (index<0 || !PtInRect(&bounds,point)) return {};
    const auto& n=tree.nodes[index];
    if (!HasChildren(n)) return {index,bounds};
    auto [a,b]=Divide(bounds,n);
    auto first=FindLeaf(tree,n.first,a,point);
    return first.index>=0?first:FindLeaf(tree,n.second,b,point);
}
struct Edge { int index{-1}; RECT rect{}; int distance{24}; };
void FindEdge(const Tree& tree,int index,RECT rect,POINT point,Edge& found) {
    if(index<0) return;
    const auto& n=tree.nodes[index];
    if(!HasChildren(n)) return;
    auto [a,b]=Divide(rect,n);
    int coordinate=n.vertical?a.right:a.bottom;
    int delta=std::abs((n.vertical?point.x:point.y)-coordinate);
    bool inside=n.vertical?(point.y>=rect.top && point.y<rect.bottom)
                          :(point.x>=rect.left && point.x<rect.right);
    if(inside && delta<found.distance) found={index,rect,delta};
    FindEdge(tree,n.first,a,point,found);
    FindEdge(tree,n.second,b,point,found);
}
// The guide window never receives input. Black is the transparent color key.
std::wstring KeyForPoint(POINT point);
constexpr wchar_t kGuideClass[]=L"ZoneSmithDynamicGuides";
void PaintSplitGuides(HDC dc,const Tree& tree,int index,RECT area,POINT origin,int active) {
    if(index<0) return;
    const auto& n=tree.nodes[index];
    if(!HasChildren(n)) return;
    auto [a,b]=Divide(area,n);
    RECT bar{};
    if(n.vertical) {
        bar={a.right-3,area.top+8,a.right+3,area.bottom-8};
    } else {
        bar={area.left+8,a.bottom-3,area.right-8,a.bottom+3};
    }
    OffsetRect(&bar,-origin.x,-origin.y);
    HBRUSH brush=CreateSolidBrush(index==active?RGB(255,198,64):RGB(20,205,240));
    FillRect(dc,&bar,brush);
    DeleteObject(brush);
    PaintSplitGuides(dc,tree,n.first,a,origin,active);
    PaintSplitGuides(dc,tree,n.second,b,origin,active);
}
LRESULT CALLBACK GuideProc(HWND hwnd,UINT message,WPARAM wp,LPARAM lp) {
    if(message==WM_NCHITTEST) return HTTRANSPARENT;
    if(message==WM_ERASEBKGND) return 1;
    if(message==WM_PAINT) {
        PAINTSTRUCT ps{};
        HDC dc=BeginPaint(hwnd,&ps);
        RECT client{};
        GetClientRect(hwnd,&client);
        FillRect(dc,&client,static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
        auto* app=reinterpret_cast<AppState*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
        if(app) {
            RECT monitor{};
            GetWindowRect(hwnd,&monitor);
            auto tree=app->dynamicTrees.find(KeyForPoint(
                POINT{(monitor.left+monitor.right)/2,(monitor.top+monitor.bottom)/2}));
            if(tree!=app->dynamicTrees.end())
                PaintSplitGuides(dc,tree->second,tree->second.root,monitor,
                                 POINT{monitor.left,monitor.top},app->dynamicDividerNode);
        }
        EndPaint(hwnd,&ps);
        return 0;
    }
    return DefWindowProcW(hwnd,message,wp,lp);
}
RECT WorkForPoint(POINT point) {
    MONITORINFO info{sizeof(info)};
    GetMonitorInfoW(MonitorFromPoint(point,MONITOR_DEFAULTTONEAREST),&info);
    return info.rcWork;
}
std::wstring KeyForPoint(POINT point) {
    MONITORINFOEXW info{sizeof(info)};
    GetMonitorInfoW(MonitorFromPoint(point,MONITOR_DEFAULTTONEAREST),&info);
    std::wstring key=info.szDevice;
    if(key.starts_with(L"\\\\.\\")) key.erase(0,4);
    std::transform(key.begin(),key.end(),key.begin(),[](wchar_t ch){return static_cast<wchar_t>(towlower(ch));});
    return key;
}
// Only edge bands select a split. The centre is not a valid docking target.
struct Drop { bool valid{}; bool vertical{}; bool before{}; RECT preview{}; };
Drop ChooseDrop(RECT area, POINT point) {
    const double w=std::max(1L,area.right-area.left);
    const double h=std::max(1L,area.bottom-area.top);
    const double x=double(point.x-area.left)/w;
    const double y=double(point.y-area.top)/h;
    const double left=x, right=1.0-x, top=y, bottom=1.0-y;
    const double nearest=std::min({left,right,top,bottom});
    if(nearest > 0.25) return {};
    const bool vertical=std::min(left,right)<=std::min(top,bottom);
    const bool first=vertical?left<right:top<bottom;
    RECT preview=area;
    if(vertical) {
        const LONG middle=(area.left+area.right)/2;
        if(first) preview.right=middle;
        else preview.left=middle;
    } else {
        const LONG middle=(area.top+area.bottom)/2;
        if(first) preview.bottom=middle;
        else preview.top=middle;
    }
    return {true,vertical,first,preview};
}
bool ContainsWindow(const Tree& tree,int index,HWND window) {
    if(index<0) return false;
    const auto& n=tree.nodes[index];
    return HasChildren(n) ? ContainsWindow(tree,n.first,window) ||
                            ContainsWindow(tree,n.second,window) : n.window==window;
}
int Insert(Tree& tree,int index,HWND window,POINT point,RECT area) {
    if(index<0) {
        tree.nodes.push_back(DynamicNode{window});
        return static_cast<int>(tree.nodes.size())-1;
    }
    auto leaf=FindLeaf(tree,index,area,point);
    if(leaf.index<0) return index;
    const auto drop=ChooseDrop(leaf.rect,point);
    if(!drop.valid) return index;
    auto current=tree.nodes[leaf.index];
    const bool vertical=drop.vertical, before=drop.before;
    const int previous=static_cast<int>(tree.nodes.size());
    tree.nodes.push_back(current);
    const int added=static_cast<int>(tree.nodes.size());
    tree.nodes.push_back(DynamicNode{window});
    tree.nodes[leaf.index]=DynamicNode{nullptr,
        before?added:previous,before?previous:added,vertical,0.5};
    return index;
}
}
bool IsDynamicLayout(const AppState& app) {
    return !app.layouts.empty() && CurrentLayout(app).name==L"Dynamic";
}
RECT DynamicPreview(const AppState& app,POINT cursor) {
    RECT work=WorkForPoint(cursor);
    auto it=app.dynamicTrees.find(KeyForPoint(cursor));
    if(it==app.dynamicTrees.end() || it->second.root<0) return work;
    // The dragged tile is removed from a COPY, making preview agree with insertion.
    Tree preview=it->second;
    if(app.dragWindow) Remove(preview,preview.root,app.dragWindow);
    if(preview.root<0) return work;
    auto leaf=FindLeaf(preview,preview.root,work,cursor);
    if(leaf.index<0) return RECT{};
    const auto drop=ChooseDrop(leaf.rect,cursor);
    return drop.valid?drop.preview:RECT{};
}
void DockDynamicWindow(AppState& app,HWND window,POINT cursor) {
    if(!window || !IsWindow(window)) return;
    const std::wstring key=KeyForPoint(cursor);
    const RECT work=WorkForPoint(cursor);
    // A drop in the centre undocks an existing tile rather than randomly
    // splitting a neighbour. Previously this silently left gaps.
    Tree preview=app.dynamicTrees[key];
    Remove(preview,preview.root,window);
    bool canDock=preview.root<0;
    if(!canDock) {
        auto leaf=FindLeaf(preview,preview.root,work,cursor);
        canDock=leaf.index>=0 && ChooseDrop(leaf.rect,cursor).valid;
    }
    bool wasTiled=false;
    for(const auto& [monitor,tree]:app.dynamicTrees)
        wasTiled=wasTiled || ContainsWindow(tree,tree.root,window);
    if(!canDock && !wasTiled) return;
    // Remove the window from its old tree AND immediately reflow that monitor.
    for(auto& [monitor,tree]:app.dynamicTrees) {
        if(Remove(tree,tree.root,window)) {
            // Resolve a monitor's bounds from one of its remaining windows.
            if(tree.root>=0 && monitor!=key) {
                HWND surviving=nullptr;
                for(const auto& node:tree.nodes) {
                    if(!HasChildren(node) && node.window && IsWindow(node.window)
                        && ContainsWindow(tree,tree.root,node.window)) {
                        surviving=node.window;
                        break;
                    }
                }
                if(surviving) {
                    MONITORINFO info{sizeof(info)};
                    if(GetMonitorInfoW(MonitorFromWindow(surviving,MONITOR_DEFAULTTONEAREST),&info))
                        Reflow(tree,tree.root,info.rcWork,app.snapPadding);
                }
            }
        }
    }
    auto& tree=app.dynamicTrees[key];
    // Prune closed windows reachable in the current tree.
    bool changed=true;
    while(changed) {
        changed=false;
        for(const auto& node:tree.nodes) {
            if(!HasChildren(node) && node.window && !IsWindow(node.window)
                && ContainsWindow(tree,tree.root,node.window)) {
                changed=Remove(tree,tree.root,node.window);
                if(changed) break;
            }
        }
    }
    if(canDock) tree.root=Insert(tree,tree.root,window,cursor,work);
    if(tree.root>=0) Reflow(tree,tree.root,work,app.snapPadding);
}
bool DetachDynamicWindow(AppState& app,HWND window) {
    if(!window) return false;
    // Capture the original monitor before the user moves the window across monitors.
    MONITORINFO info{sizeof(info)};
    if(!GetMonitorInfoW(MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST),&info))
        return false;
    bool removed=false;
    for(auto& [key,tree]:app.dynamicTrees) {
        if(!ContainsWindow(tree,tree.root,window)) continue;
        if(Remove(tree,tree.root,window)) {
            removed=true;
            if(tree.root>=0) Reflow(tree,tree.root,info.rcWork,app.snapPadding);
        }
    }
    return removed;
}
bool StartDynamicDividerDrag(AppState& app,POINT cursor) {
    if(app.paused || !IsDynamicLayout(app)) return false;
    auto key=KeyForPoint(cursor);
    auto it=app.dynamicTrees.find(key);
    if(it==app.dynamicTrees.end()) return false;
    Edge edge;
    FindEdge(it->second,it->second.root,WorkForPoint(cursor),cursor,edge);
    if(edge.index<0) return false;
    app.dynamicDividerMonitor=key;
    app.dynamicDividerNode=edge.index;
    app.dynamicDividerBounds=edge.rect;
    return true;
}
bool MoveDynamicDividerDrag(AppState& app,POINT cursor) {
    if(app.dynamicDividerNode<0) return false;
    auto it=app.dynamicTrees.find(app.dynamicDividerMonitor);
    if(it==app.dynamicTrees.end() || app.dynamicDividerNode>=static_cast<int>(it->second.nodes.size())) return false;
    auto& n=it->second.nodes[app.dynamicDividerNode];
    int span=std::max(1,Extent(app.dynamicDividerBounds,n.vertical));
    int origin=n.vertical?app.dynamicDividerBounds.left:app.dynamicDividerBounds.top;
    int position=n.vertical?cursor.x:cursor.y;
    // Respect practical minimum widths/heights instead of asking Windows to
    // repeatedly enforce the application's minimum tracking size.
    const double minRatio=std::min(0.45, 240.0/span);
    n.ratio=std::clamp(double(position-origin)/span,minRatio,1.0-minRatio);
    // The monitor is stable for a divider drag, even if the cursor leaves it.
    POINT middle{(app.dynamicDividerBounds.left+app.dynamicDividerBounds.right)/2,
                 (app.dynamicDividerBounds.top+app.dynamicDividerBounds.bottom)/2};
    Reflow(it->second,it->second.root,WorkForPoint(middle),app.snapPadding);
    if(app.dynamicGuideOverlay) InvalidateRect(app.dynamicGuideOverlay,nullptr,FALSE);
    return true;
}
void EndDynamicDividerDrag(AppState& app) {
    app.dynamicDividerNode=-1;
    app.dynamicDividerMonitor.clear();
}

void UpdateDynamicDividerGuides(AppState& app,bool enabled) {
    if(!enabled || app.paused || !IsDynamicLayout(app) || !app.overlay) {
        app.dynamicGuidesVisible=false;
        if(app.dynamicGuideOverlay) ShowWindow(app.dynamicGuideOverlay,SW_HIDE);
        return;
    }
    if(!app.dynamicGuideOverlay) {
        WNDCLASSW cls{};
        cls.hInstance=app.instance;
        cls.lpfnWndProc=GuideProc;
        cls.lpszClassName=kGuideClass;
        cls.hbrBackground=static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
        RegisterClassW(&cls);
        app.dynamicGuideOverlay=CreateWindowExW(
            WS_EX_TOPMOST|WS_EX_TOOLWINDOW|WS_EX_LAYERED|WS_EX_TRANSPARENT|WS_EX_NOACTIVATE,
            kGuideClass,L"",WS_POPUP,0,0,1,1,nullptr,nullptr,app.instance,nullptr);
        if(!app.dynamicGuideOverlay) return;
        SetWindowLongPtrW(app.dynamicGuideOverlay,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(&app));
        SetLayeredWindowAttributes(app.dynamicGuideOverlay,RGB(0,0,0),0,LWA_COLORKEY);
    }
    RECT work=WorkForPoint(app.cursor);
    // Synchronize the selected monitor so the guide renderer uses its tree.
    // Only read the monitor key; do not change persisted layout selections.
    auto key=KeyForPoint(app.cursor);
    auto selected=app.monitorLayouts.find(key);
    if(selected!=app.monitorLayouts.end() && selected->second!=L"Dynamic") {
        ShowWindow(app.dynamicGuideOverlay,SW_HIDE);
        return;
    }
    app.dynamicGuidesVisible=true;
    SetWindowPos(app.dynamicGuideOverlay,HWND_TOPMOST,work.left,work.top,
        work.right-work.left,work.bottom-work.top,
        SWP_NOACTIVATE|SWP_SHOWWINDOW);
    InvalidateRect(app.dynamicGuideOverlay,nullptr,FALSE);
}
void DestroyDynamicDividerGuides(AppState& app) {
    if(app.dynamicGuideOverlay) {
        DestroyWindow(app.dynamicGuideOverlay);
        app.dynamicGuideOverlay=nullptr;
    }
    if(app.instance) UnregisterClassW(kGuideClass,app.instance);
}

namespace {
struct MonitorLookup { const std::wstring* key; RECT work{}; bool found{}; };
BOOL CALLBACK FindMonitorByKey(HMONITOR monitor,HDC,LPRECT,LPARAM param) {
    auto& lookup=*reinterpret_cast<MonitorLookup*>(param);
    MONITORINFOEXW info{sizeof(info)};
    if(!GetMonitorInfoW(monitor,&info)) return TRUE;
    std::wstring name=info.szDevice;
    if(name.starts_with(L"\\\\.\\")) name.erase(0,4);
    std::transform(name.begin(),name.end(),name.begin(),
       [](wchar_t c){return static_cast<wchar_t>(towlower(c));});
    if(name==*lookup.key) {
        lookup.work=info.rcWork;
        lookup.found=true;
        return FALSE;
    }
    return TRUE;
}
bool MonitorWorkForKey(const std::wstring& key,RECT& work) {
    MonitorLookup lookup{&key};
    EnumDisplayMonitors(nullptr,nullptr,FindMonitorByKey,reinterpret_cast<LPARAM>(&lookup));
    if(lookup.found) work=lookup.work;
    return lookup.found;
}
}
void ReconcileDynamicWindows(AppState& app) {
    if(app.dynamicDividerNode>=0 || app.dragWindow || app.pendingDynamicWindow)
        return;
    bool changedAny=false;
    for(auto& [key,tree]:app.dynamicTrees) {
        bool changed=false;
        for(;;) {
            HWND invalid=nullptr;
            for(const auto& node:tree.nodes) {
                if(HasChildren(node) || !node.window ||
                   !ContainsWindow(tree,tree.root,node.window))
                    continue;
                if(!IsWindow(node.window) || IsIconic(node.window)) {
                    invalid=node.window;
                    break;
                }
            }
            if(!invalid || !Remove(tree,tree.root,invalid)) break;
            app.savedWindows.erase(invalid);
            changed=true;
        }
        if(changed) {
            changedAny=true;
            RECT work{};
            if(tree.root>=0 && MonitorWorkForKey(key,work))
                Reflow(tree,tree.root,work,app.snapPadding);
        }
    }
    if(changedAny && app.dynamicGuideOverlay)
        InvalidateRect(app.dynamicGuideOverlay,nullptr,FALSE);
}
void ReleaseDestroyedDynamicWindow(AppState& app,HWND window) {
    if(!window) return;
    bool changed=false;
    for(auto& [key,tree]:app.dynamicTrees) {
        if(!ContainsWindow(tree,tree.root,window)) continue;
        if(!Remove(tree,tree.root,window)) continue;
        changed=true;
        RECT work{};
        if(tree.root>=0 && MonitorWorkForKey(key,work))
            Reflow(tree,tree.root,work,app.snapPadding);
    }
    if(!changed) return;
    app.savedWindows.erase(window);
    if(app.dragWindow==window) app.dragWindow=nullptr;
    if(app.pendingDynamicWindow==window) app.pendingDynamicWindow=nullptr;
    if(app.dynamicGuideOverlay)
        InvalidateRect(app.dynamicGuideOverlay,nullptr,FALSE);
}
