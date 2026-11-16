#!/usr/bin/env python3
"""Offline OSM XML/PBF to MeshMap Nano normalized JSONL adapter."""
from __future__ import annotations
import argparse, hashlib, json
from datetime import datetime, timezone
from pathlib import Path
from typing import Any
import xml.etree.ElementTree as ET

PROVINCES=("Drenthe","Flevoland","Friesland","Gelderland","Groningen","Limburg","Noord-Brabant","Noord-Holland","Overijssel","Utrecht","Zeeland","Zuid-Holland")
def _norm(v:str)->str: return " ".join(v.casefold().replace("-", " ").split())
ALIASES={_norm(p):p for p in PROVINCES}; ALIASES.update({"brabant":"Noord-Brabant"})
HIGHWAY={"motorway":"motorway","trunk":"trunk","primary":"primary","secondary":"secondary","tertiary":"tertiary"}
WATER={"waterway","natural_water","water","reservoir","riverbank"}
class IngestError(ValueError): pass

def _province(tags):
    for key in ("province","addr:province","is_in:state","is_in:province"):
        if _norm(tags.get(key,"")) in ALIASES:return ALIASES[_norm(tags[key])]
    return None
def _kind(tags):
    if tags.get("boundary") in ("administrative","political"):return "boundary"
    if tags.get("highway") in HIGHWAY:return HIGHWAY[tags["highway"]]
    if tags.get("waterway") or tags.get("natural") in WATER:return "water"
    if tags.get("place") in ("city","town","village","hamlet","suburb"):return "place"
    return "local" if tags.get("highway") else None
def _tags(e): return {t.attrib.get("k",""):t.attrib.get("v","") for t in e.findall("tag")}

def _load_boundaries(path:Path):
    try: obj=json.loads(path.read_text(encoding="utf-8"))
    except (OSError,json.JSONDecodeError) as exc: raise IngestError(f"invalid boundary GeoJSON: {exc}") from None
    out=[]
    for f in obj.get("features",[]):
        p=_province({"province":str(f.get("properties",{}).get("province",f.get("properties",{}).get("name","")))})
        if not p: continue
        g=f.get("geometry",{}); coords=g.get("coordinates",[])
        if g.get("type")=="Polygon": polys=[coords]
        elif g.get("type")=="MultiPolygon": polys=coords
        else: continue
        out.append((p,polys))
    if {p for p,_ in out} != set(PROVINCES): raise IngestError("boundary input must contain all 12 provinces")
    return out
def _inside(point, ring):
    x,y=point; inside=False
    for i in range(len(ring)):
        x1,y1=ring[i-1]; x2,y2=ring[i]
        if ((y1>y)!=(y2>y)) and x < (x2-x1)*(y-y1)/(y2-y1)+x1: inside=not inside
    return inside
def _assign(point,boundaries):
    hits=[]
    for p,polys in boundaries:
        for poly in polys:
            if poly and _inside(point,poly[0]) and not any(_inside(point,hole) for hole in poly[1:]): hits.append(p)
    if len(hits)!=1: raise IngestError(f"coordinate does not map to exactly one province: {point}")
    return hits[0]

def parse_osm_xml(source:Path,boundaries,data_date):
    nodes={}; ways=[]; skipped=0
    for _,e in ET.iterparse(source,events=("end",)):
        if e.tag=="node":
            try: lon,lat=float(e.attrib["lon"]),float(e.attrib["lat"])
            except (KeyError,ValueError): raise IngestError("node has invalid coordinates") from None
            nodes[e.attrib["id"]]=(lon,lat,_tags(e))
        elif e.tag=="way": ways.append((e.attrib.get("id",""),_tags(e),[n.attrib.get("ref","") for n in e.findall("nd")]))
        if e.tag in ("node","way"): e.clear()
    features=[]
    for nid,(lon,lat,tags) in sorted(nodes.items()):
        kind=_kind(tags)
        if kind!="place": continue
        province=_province(tags) or _assign((lon,lat),boundaries)
        features.append({"id":f"node/{nid}","kind":kind,"province":province,"name":" ".join(tags.get("name","").split()),"zoom_min":8,"geometry":[[lon,lat]],"data_date":data_date})
    for wid,tags,refs in sorted(ways):
        kind=_kind(tags)
        if not kind: skipped+=1; continue
        if not refs or any(r not in nodes for r in refs): raise IngestError(f"way/{wid} has missing node reference")
        geometry=[[nodes[r][0],nodes[r][1]] for r in refs]
        province=_province(tags) or _assign((sum(p[0] for p in geometry)/len(geometry),sum(p[1] for p in geometry)/len(geometry)),boundaries)
        features.append({"id":f"way/{wid}","kind":kind,"province":province,"name":" ".join(tags.get("name","").split()),"zoom_min":8 if kind=="local" else 5,"geometry":geometry,"data_date":data_date})
    if not features: raise IngestError("source produced no supported features")
    return features,skipped

def parse_pbf(source,boundaries,data_date):
    try: import osmium
    except ImportError: raise IngestError("PBF input requires optional pyosmium") from None
    features=[]; skipped=0
    class Handler(osmium.SimpleHandler):
        def node(self,node):
            nonlocal skipped
            tags=dict(node.tags); kind=_kind(tags)
            if kind!="place" or not node.location.valid(): return
            point=(node.location.lon,node.location.lat); province=_province(tags) or _assign(point,boundaries)
            features.append({"id":f"node/{node.id}","kind":kind,"province":province,"name":" ".join(tags.get("name","").split()),"zoom_min":8,"geometry":[list(point)],"data_date":data_date})
        def way(self,way):
            nonlocal skipped
            tags=dict(way.tags); kind=_kind(tags)
            if not kind: skipped+=1; return
            if any(not n.location.valid() for n in way.nodes): raise IngestError(f"way/{way.id} has missing node reference")
            geometry=[[n.lon,n.lat] for n in way.nodes]
            point=(sum(p[0] for p in geometry)/len(geometry),sum(p[1] for p in geometry)/len(geometry))
            province=_province(tags) or _assign(point,boundaries)
            features.append({"id":f"way/{way.id}","kind":kind,"province":province,"name":" ".join(tags.get("name","").split()),"zoom_min":8 if kind=="local" else 5,"geometry":geometry,"data_date":data_date})
    Handler().apply_file(str(source),locations=True)
    if not features: raise IngestError("PBF produced no supported features")
    return sorted(features,key=lambda x:x["id"]),skipped

def _sha256_file(path):
    h=hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda:f.read(1024*1024),b""): h.update(chunk)
    return h.hexdigest()

def ingest(source,output,source_url,license_name,data_date,boundaries,retrieved_at=None):
    if not data_date or not isinstance(data_date,str): raise IngestError("data_date is required")
    source_path,output_path,boundary_path=Path(source),Path(output),Path(boundaries)
    boundary_data=_load_boundaries(boundary_path)
    parsed=parse_pbf(source_path,boundary_data,data_date) if source_path.suffix.lower()==".pbf" else parse_osm_xml(source_path,boundary_data,data_date)
    features,skipped=parsed; lines=b"".join(json.dumps(x,sort_keys=True,separators=(",",":")).encode()+b"\n" for x in sorted(features,key=lambda x:x["id"]))
    output_path.parent.mkdir(parents=True,exist_ok=True); output_path.write_bytes(lines)
    provenance={"contract":"meshtastic-v30-normalized-jsonl/v1","source_url":source_url,"license":license_name,"data_date":data_date,"retrieved_at":retrieved_at or datetime.now(timezone.utc).replace(microsecond=0).isoformat(),"source_sha256":_sha256_file(source_path),"boundary_sha256":_sha256_file(boundary_path),"normalized_sha256":hashlib.sha256(lines).hexdigest(),"feature_count":len(features),"retained_features":len(features),"skipped_features":skipped,"parser":"ingest_osm.py","network_access":False,"provenance_complete":True}
    output_path.with_suffix(output_path.suffix+".provenance.json").write_text(json.dumps(provenance,sort_keys=True,indent=2)+"\n",encoding="utf-8")
    return provenance

def main():
    p=argparse.ArgumentParser(); p.add_argument("source"); p.add_argument("output"); p.add_argument("boundaries"); p.add_argument("--source-url",required=True); p.add_argument("--license",required=True); p.add_argument("--data-date",required=True); p.add_argument("--retrieved-at"); a=p.parse_args()
    print(json.dumps(ingest(a.source,a.output,a.source_url,a.license,a.data_date,a.boundaries,a.retrieved_at),indent=2,sort_keys=True))
if __name__=="__main__": main()
