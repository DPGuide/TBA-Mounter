from transformers import CLIPTextModel
from diffusers import MotionAdapter
import torch

print("Lade Text-Encoder herunter...")
text_encoder = CLIPTextModel.from_pretrained("runwayml/stable-diffusion-v1-5", subfolder="text_encoder", torch_dtype=torch.float16)
text_encoder.save_pretrained("./text_encoder_lokal")

print("Lade AnimateDiff Motion Adapter herunter...")
adapter = MotionAdapter.from_pretrained("guoyww/animatediff-motion-adapter-v1-5-2", torch_dtype=torch.float16)
adapter.save_pretrained("./motion_adapter_lokal")

print("Fertig! Du hast jetzt zwei saubere Ordner.")