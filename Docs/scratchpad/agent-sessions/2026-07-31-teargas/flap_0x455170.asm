00455170  sub      esp, 0x104
00455176  push     esi
00455177  mov      esi, ecx
00455179  cmp      dword ptr [ecx + 4], 0
0045517d  jne      0x4551cf
0045517f  cmp      dword ptr [esi + 0xbc], 0
00455186  jne      0x4551cf
00455188  lea      eax, [esp + 4]
0045518c  mov      ecx, dword ptr [0x4f9948]
00455192  push     eax
00455193  push     ecx
00455194  push     0
00455196  push     8
00455198  call     0x432ab0
0045519d  add      esp, 0x10
004551a0  push     0x134
004551a5  call     0x4d28b0
004551aa  add      esp, 4
004551ad  test     eax, eax
004551af  je       0x4551c5
004551b1  lea      ecx, [esp + 4]
004551b5  push     ecx
004551b6  mov      ecx, eax
004551b8  call     0x44f860
004551bd  mov      dword ptr [esi + 0xbc], eax
004551c3  jmp      0x4551cf
004551c5  mov      dword ptr [esi + 0xbc], 0
004551cf  cmp      dword ptr [esi + 0xc0], 0
004551d6  jne      0x455253
004551d8  mov      eax, dword ptr [esi + 4]
004551db  test     eax, eax
004551dd  jne      0x4551ec
004551df  lea      eax, [esp + 4]
004551e3  mov      ecx, dword ptr [0x4f994c]
004551e9  push     eax
004551ea  jmp      0x455204
004551ec  cmp      eax, 2
004551ef  lea      eax, [esp + 4]
004551f3  push     eax
004551f4  jne      0x4551fe
004551f6  mov      ecx, dword ptr [0x4f9954]
004551fc  jmp      0x455204
004551fe  mov      ecx, dword ptr [0x4f9950]
00455204  push     ecx
00455205  push     0
00455207  push     8
00455209  call     0x432ab0
0045520e  add      esp, 0x10
00455211  push     0x134
00455216  call     0x4d28b0
0045521b  add      esp, 4
0045521e  test     eax, eax
00455220  je       0x455236
00455222  lea      ecx, [esp + 4]
00455226  push     ecx
00455227  mov      ecx, eax
00455229  call     0x44f860
0045522e  mov      dword ptr [esi + 0xc0], eax
00455234  jmp      0x455240
00455236  mov      dword ptr [esi + 0xc0], 0
00455240  mov      ecx, dword ptr [esi + 0xc0]
00455246  mov      eax, dword ptr [0x518ba8]
0045524b  push     eax
0045524c  push     1
0045524e  mov      eax, dword ptr [ecx]
00455250  call     dword ptr [eax + 8]
00455253  mov      eax, dword ptr [esi]
00455255  mov      ecx, esi
00455257  call     dword ptr [eax + 0xd8]
0045525d  mov      eax, dword ptr [esp + 0x10c]
00455264  mov      ecx, esi
00455266  push     eax
00455267  call     0x404930
0045526c  pop      esi
0045526d  add      esp, 0x104
00455273  ret      4
00455276  int3     
00455277  int3     
00455278  int3     
00455279  int3     
0045527a  int3     
0045527b  int3     
0045527c  int3     
0045527d  int3     
0045527e  int3     
0045527f  int3     
00455280  push     esi
00455281  push     edi
00455282  mov      esi, dword ptr [ecx + 0xbc]
00455288  mov      edi, ecx
0045528a  test     esi, esi
0045528c  je       0x4552a8
0045528e  mov      ecx, esi
00455290  call     0x44fbb0
00455295  push     esi
00455296  call     0x4d29a0
0045529b  mov      dword ptr [edi + 0xbc], 0
004552a5  add      esp, 4
004552a8  mov      esi, dword ptr [edi + 0xc0]
004552ae  test     esi, esi
004552b0  je       0x4552cc
004552b2  mov      ecx, esi
004552b4  call     0x44fbb0
004552b9  push     esi
004552ba  call     0x4d29a0
004552bf  mov      dword ptr [edi + 0xc0], 0
004552c9  add      esp, 4
004552cc  mov      ecx, edi
004552ce  call     0x404a20
004552d3  cmp      edi, dword ptr [0x4f81e0]
004552d9  jne      0x4552f7
004552db  push     edi
004552dc  mov      eax, dword ptr [edi]
004552de  mov      ecx, edi
004552e0  call     dword ptr [eax + 0xac]
004552e6  mov      ecx, edi
004552e8  call     0x455c10
004552ed  mov      dword ptr [edi + 0xb4], 0
004552f7  pop      edi
004552f8  pop      esi
004552f9  ret      
004552fa  int3     
004552fb  int3     
004552fc  int3     
004552fd  int3     
004552fe  int3     
004552ff  int3     
00455300  push     esi
00455301  push     edi
00455302  mov      eax, dword ptr [ecx + 4]
00455305  mov      esi, ecx
00455307  test     eax, eax
00455309  jne      0x455362
0045530b  mov      eax, dword ptr [0x4f9958]
00455310  inc      dword ptr [0x4f9958]
00455316  test     al, 3
00455318  jne      0x455393
0045531a  mov      eax, dword ptr [0x5040d0]
0045531f  mov      ecx, dword ptr [0x5040d0]
00455325  mov      eax, dword ptr [eax + 0x1d0]
0045532b  mov      edi, dword ptr [ecx]
0045532d  lea      edx, [eax + eax*4]
00455330  lea      eax, [eax + edx*2]
00455333  mov      edx, edi
00455335  lea      edi, [edi + edi*2]
00455338  shl      edi, 3
0045533b  sub      edi, edx
0045533d  cdq      
0045533e  idiv     dword ptr [edi*4 + 0x5040e8]
00455345  cmp      dword ptr [esi + 0xc4], eax
0045534b  je       0x455393
0045534d  mov      ecx, esi
0045534f  mov      dword ptr [esi + 0xc4], eax
00455355  call     0x455700
0045535a  mov      eax, 1
0045535f  pop      edi
00455360  pop      esi
00455361  ret      
00455362  cmp      eax, 3
00455365  jne      0x455393
00455367  mov      eax, dword ptr [0x4f995c]
0045536c  inc      dword ptr [0x4f995c]
00455372  test     al, 3
00455374  jne      0x455393
00455376  call     0x407a50
0045537b  mov      eax, dword ptr [eax + 0x54]
0045537e  cmp      dword ptr [esi + 0xc8], eax
00455384  je       0x455393
00455386  mov      ecx, esi
00455388  mov      dword ptr [esi + 0xc8], eax
0045538e  call     0x455790
00455393  mov      eax, 1
00455398  pop      edi
00455399  pop      esi
0045539a  ret      
0045539b  int3     
0045539c  int3     
0045539d  int3     
0045539e  int3     
0045539f  int3     
004553a0  push     esi
004553a1  mov      eax, dword ptr [0x4f81e8]
004553a6  mov      esi, ecx
004553a8  push     0x7e3
004553ad  mov      eax, dword ptr [eax]
004553af  mov      ecx, dword ptr [0x4f81e8]
004553b5  call     dword ptr [eax + 0x84]
004553bb  test     eax, eax
004553bd  je       0x4553c6
004553bf  mov      edx, dword ptr [eax]
004553c1  mov      ecx, eax
004553c3  call     dword ptr [edx + 0x7c]
004553c6  cmp      dword ptr [0x4f81e0], esi
004553cc  jne      0x4553ea
004553ce  push     esi
004553cf  mov      eax, dword ptr [esi]
004553d1  mov      ecx, esi
004553d3  call     dword ptr [eax + 0xac]
004553d9  mov      ecx, esi
004553db  call     0x455c10
