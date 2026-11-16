import { open, rename, unlink } from 'node:fs/promises';
import { dirname } from 'node:path';
import { randomBytes } from 'node:crypto';

// Single-process local candidate. Directory ownership and exclusive instance
// lock are deployment responsibilities; this is not a multi-writer database.
export class V30FileStorage {
  constructor(path) { this.path = path; }
  async load() {
    try {
      const handle = await open(this.path, 'r');
      let text;
      try {
        if ((await handle.stat()).size > 4 * 1024 * 1024) throw Error('storage_too_large');
        text = await handle.readFile('utf8');
      } finally { await handle.close(); }
      if (Buffer.byteLength(text) > 4 * 1024 * 1024) throw Error('storage_too_large');
      return JSON.parse(text);
    } catch (error) { if (error.code === 'ENOENT') return null; throw error; }
  }
  async save(state) {
    const text = JSON.stringify(state);
    if (Buffer.byteLength(text) > 4 * 1024 * 1024) throw Error('storage_too_large');
    const temp = this.path + '.' + randomBytes(8).toString('hex') + '.tmp';
    let file;
    try {
      file = await open(temp, 'wx', 0o600);
      await file.writeFile(text);
      await file.sync();
      await file.close();
      file = null;
      await rename(temp, this.path);
      const directory = await open(dirname(this.path), 'r');
      try { await directory.sync(); } finally { await directory.close(); }
    } finally {
      if (file) await file.close();
      await unlink(temp).catch(() => {});
    }
  }
}
