"""
Antigravity IDE 首页历史记录恢复补丁脚本
解决因重装或更新 IDE 导致核心文件被重置、Windows 路径 URI 转码不一致无法显示历史会话的问题。
"""

import os
import shutil
import json
import hashlib
import base64

wb_path = r'C:\Users\Mechrevo\AppData\Local\Programs\Antigravity IDE\resources\app\out\vs\workbench\workbench.desktop.main.js'
pj_path = r'C:\Users\Mechrevo\AppData\Local\Programs\Antigravity IDE\resources\app\product.json'

wb_bak = wb_path + '.bak'
pj_bak = pj_path + '.bak'

def apply_patch():
    if not os.path.exists(wb_path):
        print(f"Error: File not found: {wb_path}")
        return False

    print("Step 1: Reading workbench.desktop.main.js...")
    with open(wb_path, 'r', encoding='utf-8', newline='\n') as f:
        content = f.read()

    t1 = 'n.workspaces.map(r=>r.workspaceFolderAbsoluteUri).some(r=>e.includes(r))'
    r1 = 'n.workspaces.map(r=>r.workspaceFolderAbsoluteUri).some(r=>e.some(s=>s===r||fgt(s)===fgt(r)))'

    t2 = 'if(F.length===1&&F[0]===d.workspaceUris[0]){o(_.selectedCascadeId),setTimeout(()=>{r()},50);return}'
    r2 = 'if(F.length===1&&(F[0]===d.workspaceUris[0]||fgt(F[0])===fgt(d.workspaceUris[0]))){o(_.selectedCascadeId),setTimeout(()=>{r()},50);return}'

    t3 = 'async _isPure(){const e=this.productService.checksums||{};await this.lifecycleService.when(4);const i=await Promise.all(Object.keys(e).map(r=>this._resolve(r,e[r])));let n=!0;for(let r=0,s=i.length;r<s;r++)if(!i[r].isPure){n=!1;break}return{isPure:n,proof:i}}'
    r3 = 'async _isPure(){return{isPure:!0,proof:[]}}'

    if r1 in content and r2 in content and r3 in content:
        print("Already patched! All fixes are active.")
        return True

    if content.count(t1) != 1 or content.count(t2) != 1 or content.count(t3) != 1:
        print(f"Error: Target pattern count mismatch. t1={content.count(t1)}, t2={content.count(t2)}, t3={content.count(t3)}")
        return False

    # Backup files
    print(f"Creating/updating backup: {wb_bak}")
    shutil.copy2(wb_path, wb_bak)

    print(f"Creating/updating backup: {pj_bak}")
    shutil.copy2(pj_path, pj_bak)

    # Apply patches
    print("Step 2: Applying patches...")
    new_content = content.replace(t1, r1, 1).replace(t2, r2, 1).replace(t3, r3, 1)

    with open(wb_path, 'w', encoding='utf-8', newline='\n') as f:
        f.write(new_content)
    print("workbench.desktop.main.js patched.")

    # Update sha256 checksum in product.json
    with open(wb_path, 'rb') as f:
        data = f.read()
    new_sha = base64.b64encode(hashlib.sha256(data).digest()).decode('utf-8').rstrip('=')

    with open(pj_path, 'r', encoding='utf-8') as f:
        pj_data = json.load(f)

    if 'checksums' in pj_data:
        pj_data['checksums']['vs/workbench/workbench.desktop.main.js'] = new_sha

    with open(pj_path, 'w', encoding='utf-8', newline='\n') as f:
        json.dump(pj_data, f, indent='\t')
    print("product.json updated.")

    print("Verification:")
    with open(wb_path, 'r', encoding='utf-8') as f:
        check = f.read()
    print("  Fix 1 (Home Page filter):", r1 in check)
    print("  Fix 2 (Instant Switch):", r2 in check)
    print("  Fix 3 (_isPure bypass):", r3 in check)
    print("ALL DONE! Please reload window or restart Antigravity IDE.")
    return True

if __name__ == '__main__':
    apply_patch()
