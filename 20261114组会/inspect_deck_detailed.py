import win32com.client

ppt = win32com.client.Dispatch('PowerPoint.Application')
pres = ppt.Presentations.Open(r'D:\PyProjects\RGB_PTZ_Integrated\20261114组会\李行—组会汇报20260523.pptx', True, False, False)

print(f"Total slides: {pres.Slides.Count}")
for i in range(1, pres.Slides.Count + 1):
    s = pres.Slides(i)
    texts = []
    shapes_info = []
    for sh in s.Shapes:
        t = sh.TextFrame.TextRange.Text.strip().replace('\r', ' ').replace('\n', ' ') if sh.HasTextFrame else ''
        if t:
            texts.append(f"{sh.Name}: '{t[:30]}'")
        shapes_info.append(f"{sh.Name}(type={sh.Type})")
    print(f"--- Slide {i:02d} ---")
    print("  Shapes:", ", ".join(shapes_info))
    print("  Texts :", " | ".join(texts[:4]))

pres.Close()
ppt.Quit()

