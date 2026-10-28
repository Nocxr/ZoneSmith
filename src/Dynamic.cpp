#include "Dynamic.h"
#include "WindowUtils.h"
#include <algorithm>
#include <cmath>
#include <cwctype>
#include <cwchar>

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
struct Edge { int index{-1}; RECT rect{}; int distance{30}; };
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
// One placement operation powers both the live preview and the committed result.
// The outer 8% of a monitor adds a split above/beside the entire layout.
// Otherwise the outer 25% of the hovered tile splits only that tile.
struct Placement {
    bool valid{};
    bool rootSplit{};
    bool swap{};
    bool vertical{};
    bool before{};
    int leaf{-1};
    RECT destination{};
};
Placement ResolvePlacement(const Tree& tree,RECT work,POINT point) {
    if(!PtInRect(&work,point)) return {};
    if(tree.root<0) return {true,false,false,true,true,-1,work};
    const double x=double(point.x-work.left)/std::max(1L,work.right-work.left);
    const double y=double(point.y-work.top)/std::max(1L,work.bottom-work.top);
    const double edge=std::min({x,1.0-x,y,1.0-y});
    if(edge<=0.08) {
        const bool vertical=std::min(x,1.0-x)<=std::min(y,1.0-y);
        const bool before=vertical?x<0.5:y<0.5;
        RECT destination=work;
        if(vertical) {
            LONG mid=(work.left+work.right)/2;
            if(before) destination.right=mid;
            else destination.left=mid;
        } else {
            LONG mid=(work.top+work.bottom)/2;
            if(before) destination.bottom=mid;
            else destination.top=mid;
        }
        return {true,true,false,vertical,before,-1,destination};
    }
    const auto leaf=FindLeaf(tree,tree.root,work,point);
    if(leaf.index<0) return {};
    const auto drop=ChooseDrop(leaf.rect,point);
    if(drop.valid)
        return {true,false,false,drop.vertical,drop.before,leaf.index,drop.preview};
    // Center of an occupied tile swaps the dragged window with its occupant.
    return {true,false,true,false,false,leaf.index,leaf.rect};
}
RECT PreviewPlacement(const Tree& source,RECT work,POINT point) {
    return ResolvePlacement(source,work,point).destination;
}
bool ApplyPlacement(Tree& tree,HWND window,RECT work,POINT point) {
    const Placement placement=ResolvePlacement(tree,work,point);
    if(!placement.valid) return false;
    if(tree.root<0) {
        tree.nodes.push_back(DynamicNode{window});
        tree.root=static_cast<int>(tree.nodes.size())-1;
        return true;
    }
    if(placement.swap) {
        if(placement.leaf<0) return false;
        tree.nodes[placement.leaf].window=window;
        return true;
    }
    const int previous=placement.rootSplit?tree.root:placement.leaf;
    if(previous<0) return false;
    DynamicNode current=tree.nodes[previous];
    const int previousCopy=static_cast<int>(tree.nodes.size());
    tree.nodes.push_back(current);
    const int newLeaf=static_cast<int>(tree.nodes.size());
    tree.nodes.push_back(DynamicNode{window});
    const DynamicNode split{nullptr,
        placement.before?newLeaf:previousCopy,
        placement.before?previousCopy:newLeaf,
        placement.vertical,0.5};
    if(placement.rootSplit) {
        tree.nodes.push_back(split);
        tree.root=static_cast<int>(tree.nodes.size())-1;
    } else tree.nodes[previous]=split;
    return true;
}
}
bool IsDynamicLayout(const AppState& app) {
    return !app.layouts.empty() && CurrentLayout(app).name==L"Dynamic";
}
RECT DynamicPreview(const AppState& app,POINT cursor) {
    const RECT work=WorkForPoint(cursor);
    auto it=app.dynamicTrees.find(KeyForPoint(cursor));
    if(it==app.dynamicTrees.end() || it->second.root<0) return work;
    Tree preview=it->second;
    if(app.dragWindow) Remove(preview,preview.root,app.dragWindow);
    return PreviewPlacement(preview,work,cursor);
}
void DockDynamicWindow(AppState& app,HWND window,POINT cursor) {
    if(!window || !IsWindow(window)) return;
    const std::wstring key=KeyForPoint(cursor);
    const RECT work=WorkForPoint(cursor);
    Tree proposal=app.dynamicTrees[key];
    Remove(proposal,proposal.root,window);
    const auto placement=ResolvePlacement(proposal,work,cursor);
    if(!placement.valid) return;
    // Swapping an occupied leaf keeps the displaced window in the source slot
    // when one exists. Otherwise it remains a floating window.
    HWND displaced=nullptr;
    if(placement.swap && placement.leaf>=0)
        displaced=proposal.nodes[placement.leaf].window;
    if(!ApplyPlacement(proposal,window,work,cursor)) return;
    for(auto& [monitor,tree]:app.dynamicTrees) {
        if(monitor==key) continue;
        if(!ContainsWindow(tree,tree.root,window)) continue;
        Remove(tree,tree.root,window);
        if(displaced) {
            if(tree.root<0) {
                tree.nodes.push_back(DynamicNode{displaced});
                tree.root=static_cast<int>(tree.nodes.size())-1;
            } else {
                // Swapping back into a source tile is preferable to dropping it.
                auto old=FindLeaf(tree,tree.root,work,cursor);
                if(old.index>=0) tree.nodes[old.index].window=displaced;
            }
        }
        // Source-monitor reflow uses its own display bounds.
        for(const auto& node:tree.nodes) {
            if(!HasChildren(node) && node.window &&
               ContainsWindow(tree,tree.root,node.window) && IsWindow(node.window)) {
                MONITORINFO info{sizeof(info)};
                if(GetMonitorInfoW(MonitorFromWindow(node.window,MONITOR_DEFAULTTONEAREST),&info))
                    Reflow(tree,tree.root,info.rcWork,app.snapPadding);
                break;
            }
        }
    }
    app.dynamicTrees[key]=std::move(proposal);
    Reflow(app.dynamicTrees[key],app.dynamicTrees[key].root,work,app.snapPadding);
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
    if(edge.index<0) {
        wchar_t log[256]{};
        swprintf_s(log,L"[ZoneSmith divider] MISS x=%ld y=%ld root=%d\\n",
                   cursor.x,cursor.y,it->second.root);
        OutputDebugStringW(log);
        return false;
    }
    app.dynamicDividerMonitor=key;
    app.dynamicDividerNode=edge.index;
    app.dynamicDividerBounds=edge.rect;
    if(app.dividerGuideWindow) InvalidateRect(app.dividerGuideWindow,nullptr,FALSE);
    ++app.dynamicDividerCaptureCount;
    wchar_t log[256]{};
    swprintf_s(log,L"[ZoneSmith divider] CAPTURE node=%d x=%ld y=%ld tolerance=30\\n",
               edge.index,cursor.x,cursor.y);
    OutputDebugStringW(log);
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
    n.ratio=std::clamp(double(position-origin)/span,0.15,0.85);
    // The monitor is stable for a divider drag, even if the cursor leaves it.
    POINT middle{(app.dynamicDividerBounds.left+app.dynamicDividerBounds.right)/2,
                 (app.dynamicDividerBounds.top+app.dynamicDividerBounds.bottom)/2};
    app.dynamicDividerApplying=true;
    Reflow(it->second,it->second.root,WorkForPoint(middle),app.snapPadding);
    app.dynamicDividerApplying=false;
    if(app.dividerGuideWindow) InvalidateRect(app.dividerGuideWindow,nullptr,FALSE);
    ++app.dynamicDividerApplyCount;
    wchar_t log[256]{};
    swprintf_s(log,L"[ZoneSmith divider] APPLY node=%d ratio=%.3f moves=%u applies=%u\\n",
               app.dynamicDividerNode,n.ratio,app.dynamicDividerMoveCount,app.dynamicDividerApplyCount);
    OutputDebugStringW(log);
    return true;
}
void EndDynamicDividerDrag(AppState& app) {
    wchar_t log[256]{};
    swprintf_s(log,L"[ZoneSmith divider] END captures=%u misses=%u moves=%u applies=%u\\n",
               app.dynamicDividerCaptureCount,app.dynamicDividerMissCount,
               app.dynamicDividerMoveCount,app.dynamicDividerApplyCount);
    OutputDebugStringW(log);
    app.dynamicDividerNode=-1;
    app.dynamicDividerMonitor.clear();
    if(app.dividerGuideWindow) InvalidateRect(app.dividerGuideWindow,nullptr,FALSE);
}

namespace {
constexpr wchar_t kDividerGuideClass[] = L"ZoneSmithDividerGuide";
void DrawDividerLines(HDC dc,const DynamicTree& tree,int index,RECT area,POINT origin,int active) {
    if(index<0 || index>=static_cast<int>(tree.nodes.size())) return;
    const auto& node=tree.nodes[index];
    if(!HasChildren(node)) return;
    auto [first,second]=Divide(area,node);
    RECT bar=node.vertical ?
        RECT{first.right-4,area.top+4,first.right+4,area.bottom-4} :
        RECT{area.left+4,first.bottom-4,area.right-4,first.bottom+4};
    OffsetRect(&bar,-origin.x,-origin.y);
    HBRUSH brush=CreateSolidBrush(index==active?RGB(255,184,35):RGB(35,214,239));
    FillRect(dc,&bar,brush);
    DeleteObject(brush);
    DrawDividerLines(dc,tree,node.first,first,origin,active);
    DrawDividerLines(dc,tree,node.second,second,origin,active);
}
LRESULT CALLBACK DividerGuideProc(HWND hwnd,UINT message,WPARAM wp,LPARAM lp) {
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
            RECT bounds{};
            GetWindowRect(hwnd,&bounds);
            POINT point{bounds.left+(bounds.right-bounds.left)/2,
                        bounds.top+(bounds.bottom-bounds.top)/2};
            auto tree=app->dynamicTrees.find(KeyForPoint(point));
            if(tree!=app->dynamicTrees.end()) {
                const int active=app->dynamicDividerMonitor==KeyForPoint(point)
                    ?app->dynamicDividerNode:-1;
                DrawDividerLines(dc,tree->second,tree->second.root,bounds,
                                 POINT{bounds.left,bounds.top},active);
            }
        }
        EndPaint(hwnd,&ps);
        return 0;
    }
    return DefWindowProcW(hwnd,message,wp,lp);
}
}
bool IsNearDynamicDivider(const AppState& app,POINT cursor) {
    auto it=app.dynamicTrees.find(KeyForPoint(cursor));
    if(it==app.dynamicTrees.end() || it->second.root<0) return false;
    Edge edge;
    FindEdge(it->second,it->second.root,WorkForPoint(cursor),cursor,edge);
    return edge.index>=0;
}
void ShowDynamicDividerGuides(AppState& app,bool enabled) {
    if(!enabled || app.paused) {
        app.dividerGuideVisible=false;
        if(app.dividerGuideWindow) ShowWindow(app.dividerGuideWindow,SW_HIDE);
        return;
    }
    auto key=KeyForPoint(app.cursor);
    auto selected=app.monitorLayouts.find(key);
    if(selected!=app.monitorLayouts.end() && selected->second!=L"Dynamic") {
        if(app.dividerGuideWindow) ShowWindow(app.dividerGuideWindow,SW_HIDE);
        app.dividerGuideVisible=false;
        return;
    }
    if(app.dynamicTrees.find(key)==app.dynamicTrees.end() ||
       app.dynamicTrees.at(key).root<0) return;
    if(!app.dividerGuideWindow) {
        WNDCLASSW cls{};
        cls.hInstance=app.instance;
        cls.lpfnWndProc=DividerGuideProc;
        cls.lpszClassName=kDividerGuideClass;
        RegisterClassW(&cls);
        app.dividerGuideWindow=CreateWindowExW(
            WS_EX_TOPMOST|WS_EX_LAYERED|WS_EX_TRANSPARENT|
            WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,
            kDividerGuideClass,L"",WS_POPUP,0,0,1,1,
            nullptr,nullptr,app.instance,nullptr);
        if(!app.dividerGuideWindow) return;
        SetWindowLongPtrW(app.dividerGuideWindow,GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(&app));
        SetLayeredWindowAttributes(app.dividerGuideWindow,RGB(0,0,0),0,LWA_COLORKEY);
    }
    RECT bounds=WorkForPoint(app.cursor);
    SetWindowPos(app.dividerGuideWindow,HWND_TOPMOST,bounds.left,bounds.top,
                 bounds.right-bounds.left,bounds.bottom-bounds.top,
                 SWP_NOACTIVATE|SWP_SHOWWINDOW);
    app.dividerGuideVisible=true;
    InvalidateRect(app.dividerGuideWindow,nullptr,FALSE);
}
void DestroyDynamicDividerGuides(AppState& app) {
    if(app.dividerGuideWindow) {
        DestroyWindow(app.dividerGuideWindow);
        app.dividerGuideWindow=nullptr;
    }
    if(app.instance) UnregisterClassW(kDividerGuideClass,app.instance);
}

void ReconcileDynamicWindowStates(AppState& app) {
    // Preserve the original tree when minimizing. Reflow a temporary active
    // tree so restoring a window puts it back in its original split.
    if(app.dynamicDividerNode>=0 || app.dynamicReflowApplying ||
       app.dragWindow || app.pendingDynamicWindow) return;
    for(auto& [key,tree]:app.dynamicTrees) {
        if(tree.root<0) continue;
        bool changed=false;
        for(;;) {
            HWND dead=nullptr;
            for(const auto& node:tree.nodes) {
                if(HasChildren(node) || !node.window ||
                   !ContainsWindow(tree,tree.root,node.window)) continue;
                if(!IsWindow(node.window)) { dead=node.window; break; }
            }
            if(!dead || !Remove(tree,tree.root,dead)) break;
            app.dynamicMinimizedStates.erase(dead);
            changed=true;
        }
        if(tree.root<0) continue;
        Tree active=tree;
        for(const auto& node:tree.nodes) {
            if(HasChildren(node) || !node.window ||
               !ContainsWindow(tree,tree.root,node.window)) continue;
            const bool minimized=IsIconic(node.window)!=FALSE;
            auto [it,inserted]=app.dynamicMinimizedStates.emplace(node.window,false);
            if(it->second!=minimized) {
                changed=true;
                it->second=minimized;
            }
            if(minimized) Remove(active,active.root,node.window);
        }
        if(!changed) continue;
        // Look up owning monitor from its name, not an application that may
        // currently be minimized or moved.
        RECT work{};
        bool found=false;
        struct Context { const std::wstring* key; RECT* work; bool* found; };
        Context context{&key,&work,&found};
        EnumDisplayMonitors(nullptr,nullptr,
            [](HMONITOR monitor,HDC,LPRECT,LPARAM param)->BOOL {
                auto& ctx=*reinterpret_cast<Context*>(param);
                MONITORINFOEXW info{sizeof(info)};
                if(!GetMonitorInfoW(monitor,&info)) return TRUE;
                std::wstring name=info.szDevice;
                if(name.starts_with(L"\\\\.\\")) name.erase(0,4);
                std::transform(name.begin(),name.end(),name.begin(),
                    [](wchar_t c){return static_cast<wchar_t>(towlower(c));});
                if(name!=*ctx.key) return TRUE;
                *ctx.work=info.rcWork;
                *ctx.found=true;
                return FALSE;
            },reinterpret_cast<LPARAM>(&context));
        if(found && active.root>=0) {
            app.dynamicReflowApplying=true;
            Reflow(active,active.root,work,app.snapPadding);
            app.dynamicReflowApplying=false;
        }
        if(app.dividerGuideWindow)
            InvalidateRect(app.dividerGuideWindow,nullptr,FALSE);
    }
}
