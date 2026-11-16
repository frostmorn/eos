#!/usr/bin/env python3
"""Call graph for EOS sources: build a JSON graph, then render focused views.

  python3 tools/callgraph.py build [--root main] [--out callgraph.json]
  python3 tools/callgraph.py focus eos_vfs_open_r --callees 3 --callers 1 --out vfs_open.svg
  python3 tools/callgraph.py find open            # list functions matching a substring

Edges: direct (solid), indirect through function-pointer tables (dashed, labeled
with the field, e.g. ".open"), and linker --wrap redirects (dotted: a call to X
goes to __wrap_X when the repo defines it). Indirect edges are resolved by the struct type of the receiver
(e.g. mount->ops.open uses the eos_vfs_ops_t initializers); when the type cannot be
determined they fall back to the field name alone and are marked with '?'
(hidden in `focus` unless --guess is given).
Needs: pip install tree-sitter tree-sitter-c; Graphviz `dot` for SVG output.
"""
import argparse, json, os, subprocess, sys
from collections import defaultdict, deque
import tree_sitter_c
from tree_sitter import Language, Parser

sys.setrecursionlimit(20000)
txt = lambda n: n.text.decode(errors="replace")
PALETTE = ["#cfe8ff", "#ffe3b3", "#d5f5d0", "#f3d1f5", "#ffd1d1", "#e0e0e0", "#fff5b3", "#c9f2ee"]


def fn_name(defn):
    d = defn.child_by_field_name("declarator")
    while d is not None and d.type != "function_declarator":
        d = d.child_by_field_name("declarator")
    i = d.child_by_field_name("declarator") if d is not None else None
    return txt(i) if i is not None and i.type == "identifier" else None


def tname(n):
    """Base type name of a type node (struct tag, typedef name, primitive)."""
    if n is None:
        return None
    if n.type == "struct_specifier":
        nm = n.child_by_field_name("name")
        return txt(nm) if nm is not None else None
    return txt(n)


def dname(d):
    """Variable/field name inside a (possibly pointer/array) declarator."""
    while d is not None and d.type in ("pointer_declarator", "array_declarator", "parenthesized_declarator", "init_declarator"):
        d = d.child_by_field_name("declarator")
    return txt(d) if d is not None and d.type in ("identifier", "field_identifier") else None


def struct_fields(body):
    out = {}
    for fd in body.children:
        if fd.type == "field_declaration":
            t = tname(fd.child_by_field_name("type"))
            for dc in fd.children_by_field_name("declarator"):
                nm = dname(dc)
                if nm:
                    out[nm] = t
    return out


def walk(n):
    yield n
    for c in n.children:
        yield from walk(c)


def build(root, out):
    parser = Parser(Language(tree_sitter_c.language()))
    paths = [os.path.join(dp, f) for dp, _, fs in os.walk(root) for f in sorted(fs) if f.endswith((".c", ".h"))]
    trees = {p: parser.parse(open(p, "rb").read()).root_node for p in paths}

    # pass 1: struct layouts from .c and .h (struct tag and typedef name -> {field: type})
    structs = {}
    for root_node in trees.values():
        for n in walk(root_node):
            if n.type == "struct_specifier" and n.child_by_field_name("body") is not None:
                fields = struct_fields(n.child_by_field_name("body"))
                if n.child_by_field_name("name") is not None:
                    structs[txt(n.child_by_field_name("name"))] = fields
                if n.parent is not None and n.parent.type == "type_definition":
                    structs[txt(n.parent.child_by_field_name("declarator"))] = fields

    funcs, calls = {}, []
    typed, untyped = defaultdict(set), defaultdict(set)   # (type, field) -> funcs ; field -> funcs
    gvars = {}

    def type_of(e, vars_):
        if e is None:
            return None
        if e.type == "identifier":
            return vars_.get(txt(e)) or gvars.get(txt(e))
        if e.type == "field_expression":
            t = type_of(e.child_by_field_name("argument"), vars_)
            return structs.get(t, {}).get(txt(e.child_by_field_name("field")))
        if e.type in ("parenthesized_expression", "pointer_expression", "subscript_expression", "cast_expression"):
            return type_of(e.child_by_field_name("argument") or e.child_by_field_name("value") or (e.children[1] if len(e.children) > 1 else None), vars_)
        return None

    def init_list(t, node):
        for c in node.children:
            if c.type == "initializer_pair":
                des = [x for x in c.children if x.type == "field_designator"]
                val = c.child_by_field_name("value")
                if not des or val is None:
                    continue
                f = txt(des[0].children[-1])
                if val.type == "identifier":
                    typed[(t, f)].add(txt(val))
                    untyped[f].add(txt(val))
                elif val.type == "initializer_list":
                    init_list(structs.get(t, {}).get(f), val)
            elif c.type == "initializer_list":
                init_list(t, c)

    def declare(n, vars_):
        t = tname(n.child_by_field_name("type"))
        for dc in n.children_by_field_name("declarator"):
            nm = dname(dc)
            if nm:
                vars_[nm] = t
            if dc.type == "init_declarator":
                v = dc.child_by_field_name("value")
                if v is not None and v.type == "initializer_list":
                    init_list(t, v)

    def visit(n, cur, rel, vars_):
        t = n.type
        if t == "function_definition":
            name = fn_name(n)
            if name:
                cur, vars_ = name, {}
                funcs[name] = {"file": rel, "line": n.start_point[0] + 1, "module": os.path.dirname(rel)}
                for pd in (x for x in walk(n.child_by_field_name("declarator")) if x.type == "parameter_declaration"):
                    nm = dname(pd.child_by_field_name("declarator"))
                    if nm:
                        vars_[nm] = tname(pd.child_by_field_name("type"))
        elif t == "declaration":
            declare(n, vars_ if cur else gvars)
        elif t == "call_expression" and cur:
            f = n.child_by_field_name("function")
            if f is not None and f.type == "identifier":
                calls.append((cur, txt(f), "direct", None))
            elif f is not None and f.type == "field_expression":
                calls.append((cur, txt(f.child_by_field_name("field")), "indirect", type_of(f.child_by_field_name("argument"), vars_)))
        elif t == "assignment_expression":
            l, r = n.child_by_field_name("left"), n.child_by_field_name("right")
            if l is not None and r is not None and l.type == "field_expression" and r.type == "identifier":
                fld = txt(l.child_by_field_name("field"))
                typed[(type_of(l.child_by_field_name("argument"), vars_), fld)].add(txt(r))
                untyped[fld].add(txt(r))
        for c in n.children:
            visit(c, cur, rel, vars_)

    nfiles = 0
    for p, root_node in trees.items():
        if p.endswith(".c"):
            visit(root_node, None, os.path.relpath(p, root), {})
            nfiles += 1

    edges = {}
    for caller, callee, kind, rtype in calls:
        if kind == "direct":
            w = "__wrap_" + callee
            if w in funcs and caller != w:
                edges[(caller, w, "wrap", "")] = 1
            if callee in funcs and callee != caller:
                edges[(caller, callee, "direct", "")] = 1
        else:
            targets, via = typed.get((rtype, callee)), "." + callee
            if not targets:
                targets, via = untyped.get(callee, ()), "." + callee + "?"
            for tgt in targets:
                if tgt in funcs and tgt != caller:
                    edges[(caller, tgt, "indirect", via)] = 1
    graph = {"nodes": funcs, "edges": [{"from": a, "to": b, "kind": k, "via": v} for (a, b, k, v) in sorted(edges)]}
    json.dump(graph, open(out, "w"), indent=0)
    kinds = defaultdict(int)
    for e in graph["edges"]:
        kinds[e["kind"]] += 1
    guessed = sum(1 for e in graph["edges"] if e["via"].endswith("?"))
    print(f"{nfiles} files, {len(funcs)} functions, edges: {dict(kinds)} ({guessed} indirect edges resolved by field name only, marked '?') -> {out}")


def focus(graph_file, name, callees, callers, no_indirect, out, limit, guess=False):
    g = json.load(open(graph_file))
    if name not in g["nodes"]:
        sys.exit(f"unknown function '{name}' (try: find <substring>)")
    fwd, rev = defaultdict(list), defaultdict(list)
    for e in g["edges"]:
        if (no_indirect and e["kind"] == "indirect") or (not guess and e["via"].endswith("?")):
            continue
        fwd[e["from"]].append(e)
        rev[e["to"]].append(e)
    keep, used = {name}, []

    def bfs(adj, depth, nxt):
        q = deque([(name, 0)])
        seen = {name}
        while q:
            n, d = q.popleft()
            if d >= depth:
                continue
            for e in adj[n]:
                m = e[nxt]
                used.append(e)
                if m not in seen:
                    seen.add(m)
                    keep.add(m)
                    q.append((m, d + 1))

    bfs(fwd, callees, "to")
    bfs(rev, callers, "from")
    if len(keep) > limit:
        print(f"warning: {len(keep)} nodes, drawing may be unreadable (lower --callees/--callers or use --no-indirect)")
    mods = sorted({g["nodes"][n]["module"].split("/")[0] for n in keep})
    col = {m: PALETTE[i % len(PALETTE)] for i, m in enumerate(mods)}
    L = ["digraph G {", " rankdir=LR; node [shape=box, style=filled, fontname=Helvetica, fontsize=10]; edge [fontsize=8];"]
    for n in sorted(keep):
        i = g["nodes"][n]
        pen = ", penwidth=3" if n == name else ""
        L.append(f' "{n}" [fillcolor="{col[i["module"].split("/")[0]]}", tooltip="{i["file"]}:{i["line"]}", label="{n}\\n{i["module"]}"{pen}];')
    style = {"direct": "", "indirect": 'style=dashed, color="#1f5fbf"', "wrap": 'style=dotted, color="#2a8a2a"'}
    for e in {(e["from"], e["to"], e["kind"], e["via"]): e for e in used}.values():
        lab = f', label="{e["via"]}"' if e["via"] else ""
        L.append(f' "{e["from"]}" -> "{e["to"]}" [{style[e["kind"]]}{lab}];')
    L.append("}")
    dot = "\n".join(L)
    if out.endswith(".dot"):
        open(out, "w").write(dot)
    else:
        subprocess.run(["dot", "-T" + out.rsplit(".", 1)[-1], "-o", out], input=dot.encode(), check=True)
    print(f"{len(keep)} functions, {len(set(map(id, used)))} edges -> {out}")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    b = sub.add_parser("build"); b.add_argument("--root", default="main"); b.add_argument("--out", default="callgraph.json")
    f = sub.add_parser("focus"); f.add_argument("name"); f.add_argument("--graph", default="callgraph.json")
    f.add_argument("--callees", type=int, default=2); f.add_argument("--callers", type=int, default=0)
    f.add_argument("--no-indirect", action="store_true"); f.add_argument("--guess", action="store_true", help="also draw indirect edges matched by field name only ('?')"); f.add_argument("--out", default="focus.svg"); f.add_argument("--limit", type=int, default=120)
    s = sub.add_parser("find"); s.add_argument("text"); s.add_argument("--graph", default="callgraph.json")
    a = ap.parse_args()
    if a.cmd == "build":
        build(a.root, a.out)
    elif a.cmd == "focus":
        focus(a.graph, a.name, a.callees, a.callers, a.no_indirect, a.out, a.limit, a.guess)
    else:
        for n, i in sorted(json.load(open(a.graph))["nodes"].items()):
            if a.text in n:
                print(f"{n:48s} {i['file']}:{i['line']}")


if __name__ == "__main__":
    main()
