import argparse
import scipy.io.wavfile
import torch
from transformers import AutoProcessor, MusicgenForConditionalGeneration

parser = argparse.ArgumentParser()
parser.add_argument("--prompt", type=str, required=True)
parser.add_argument("--duration", type=int, default=10)
parser.add_argument("--output", type=str, default="output_beat.wav")
args = parser.parse_args()

print("[BASTION] Lade MusicGen via Hugging Face (Transformers)...")

# 1. Den Übersetzer und das Modell laden (Small-Version für die GTX 1060)
processor = AutoProcessor.from_pretrained("facebook/musicgen-small")
model = MusicgenForConditionalGeneration.from_pretrained("facebook/musicgen-small")

# --- DER GTX 1060 FIX (Kein NaN-Fehler mehr!) ---
model.to(torch.float32)

print(f"[BASTION] Generiere Beat für: {args.prompt}")

# 2. Den Prompt für die KI vorbereiten
inputs = processor(
    text=[args.prompt],
    padding=True,
    return_tensors="pt",
)

# 3. Dauer berechnen (MusicGen braucht ca. 50 Tokens pro Sekunde Audio)
max_tokens = int(args.duration * 50)

# 4. Audio berechnen (Hier schwitzt die Grafikkarte!)
audio_values = model.generate(**inputs, max_new_tokens=max_tokens)

# 5. Speichern
sampling_rate = model.config.audio_encoder.sampling_rate
audio_data = audio_values[0, 0].cpu().numpy()

# Sicherstellen, dass .wav am Ende steht
output_path = args.output if args.output.endswith(".wav") else f"{args.output}.wav"

scipy.io.wavfile.write(output_path, rate=sampling_rate, data=audio_data)

print(f"[SUCCESS] Beat erstellt: {output_path}")