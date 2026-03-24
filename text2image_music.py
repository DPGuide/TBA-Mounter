import tkinter as tk
from tkinter import filedialog, Scale, messagebox
import threading
import torch
import ctypes
import sys
import os
os.environ["PYTORCH_CUDA_ALLOC_CONF"] = "expandable_segments:True"
import time
import random 
from PIL import Image as PILImage
import gc  # Garbage Collector
import subprocess

print("!!! GRAFIKKARTEN-TEST: ", torch.cuda.is_available(), " !!!")
print("Ist CUDA aktiv?", torch.cuda.is_available())

# KI Bibliotheken
from diffusers import (
    StableDiffusionPipeline, 
    StableDiffusionImg2ImgPipeline, 
    AnimateDiffPipeline, 
    MotionAdapter,
    LCMScheduler # Das Turbo-Getriebe
)
from diffusers.utils import export_to_gif, export_to_video
from transformers import CLIPTextModel

# --- ADMIN CHECK & NEUSTART ---
def is_admin():
    try:
        return ctypes.windll.shell32.IsUserAnAdmin()
    except:
        return False

if not is_admin():
    print("Fordere Admin-Rechte an...")
    ctypes.windll.shell32.ShellExecuteW(None, "runas", sys.executable, " ".join(sys.argv), None, 1)
    sys.exit(0)

# --- HAUPTKLASSE ---
class ImageGenGUI:
    def __init__(self, root):
        self.root = root
        self.root.title("GTX 1060 - Ultimate Mounter Edition (ADMIN)")
        self.root.geometry("1000x950") 
        self.root.configure(bg="#1e1e1e")

        # --- Variablen mit Standardwerten ---
        self.model_path = tk.StringVar(value="Z:\\Models\\v1-5-pruned-emaonly.safetensors")
        self.lora_path = tk.StringVar()
        self.input_image_path = tk.StringVar()
        self.text_encoder_path = tk.StringVar(value="Z:\\Models\\text_encoder_lokal")
        self.motion_adapter_path = tk.StringVar(value="Z:\\Models\\motion_adapter_lokal")
 
        self.prompt = tk.StringVar(value="smooth, seamless motion transitions, super good sharped detailed focus view, highly cinematic detailed")
        self.neg_prompt = tk.StringVar(value="bad smooth motion transitions, bad focus, bad detailed, no haze, six fingers, ugly, deformed, blured, unsharp text, watermark")
        
        self.seed_var = tk.StringVar(value="-1") 
        
        self.setup_ui()

    def cleanup(self):
        """Leert den Speicher, um VRAM-Abstürze bei 6GB Karten zu verhindern."""
        if hasattr(self, 'pipe'): del self.pipe
        if hasattr(self, 'text_encoder'): del self.text_encoder
        if hasattr(self, 'adapter'): del self.adapter
        
        gc.collect()
        if torch.cuda.is_available():
            torch.cuda.empty_cache()
            torch.cuda.ipc_collect()
        
    def start_rennauto(self, basis_text="Generation starts..."):
        self.is_racing = True
        self.car_pos = 0
        self.track_length = 20
        self.basis_text = basis_text
        self.animate_rennauto()

    def animate_rennauto(self):
        if not getattr(self, "is_racing", False):
            return # Motor aus, Animation stoppen
            
        # Strecke bauen und Auto setzen
        track = ["-"] * self.track_length
        # Ping-Pong Logik (fährt hin und zurück)
        ping_pong_pos = self.car_pos % (self.track_length * 2)
        if ping_pong_pos >= self.track_length:
            actual_pos = (self.track_length * 2) - 1 - ping_pong_pos
            car = "🏎️" # Fährt nach links
        else:
            actual_pos = ping_pong_pos
            car = "🏎️" # Fährt nach rechts
            
        track[actual_pos] = car
        track_str = "".join(track)
        
        # Label aktualisieren
        self.status_label.config(text=f"{self.basis_text}\n[{track_str}]", fg="orange")
        
        self.car_pos += 1
        # In 150 Millisekunden das nächste Bild aufrufen
        self.status_label.after(150, self.animate_rennauto)
        
    def stop_rennauto(self, end_text="Done!", color="#2ecc71"):
        self.is_racing = False
        for b in [self.btn_img, self.btn_gif, self.btn_mp4]: 
            b.config(state="normal")
        
        self.status_label.config(text=end_text, fg=color)

    def setup_ui(self):
        # Header
        tk.Label(self.root, text="STABLE DIFFUSION - MOUNTER CONTROL", fg="white", bg="#1e1e1e", font=("Arial", 14, "bold")).pack(pady=10)

        # Auswahl-Sektion
        self.add_file_selector("1. Basic model (.safetensors):", self.model_path, self.browse_model)
        self.add_file_selector("2. LoRA / LCM-Turbo LoRA:", self.lora_path, self.browse_lora)
        self.add_file_selector("3. Template image (Optional for Img2Img):", self.input_image_path, self.browse_input_image)
        
        tk.Label(self.root, text="--- Offline Mounter Paths ---", fg="#888", bg="#1e1e1e", font=("Arial", 8, "italic")).pack(pady=(15,0))
        self.add_file_selector("A. Text encoder folder:", self.text_encoder_path, self.browse_text_encoder)
        self.add_file_selector("B. Motion adapter folder:", self.motion_adapter_path, self.browse_motion_adapter)

        # Prompts & Seed
        self.add_text_entry("Prompt (What should be shown?)):", self.prompt, 15)
        self.add_text_entry("Negative Prompt (What should NOT be visible?)):", self.neg_prompt, 5)
        self.add_text_entry("Seed (-1 for pure randomness):", self.seed_var, 5) 

        # Regler Sektion
        slider_frame = tk.Frame(self.root, bg="#1e1e1e")
        slider_frame.pack(fill="x", padx=20, pady=20)
        
        # Steps
        tk.Label(slider_frame, text="Steps:", fg="white", bg="#1e1e1e", font=("Arial", 10, "bold")).grid(row=0, column=0, sticky="w")
        self.slider_steps = Scale(slider_frame, from_=1, to=50, orient="horizontal", bg="#1e1e1e", fg="white", highlightthickness=0); self.slider_steps.set(14); self.slider_steps.grid(row=1, column=0, padx=5, sticky="we")
        
        # Breite
        tk.Label(slider_frame, text="Width:", fg="white", bg="#1e1e1e", font=("Arial", 10, "bold")).grid(row=0, column=1, sticky="w")
        self.slider_width = Scale(slider_frame, from_=256, to=1280, orient="horizontal", resolution=64, bg="#1e1e1e", fg="white", highlightthickness=0); self.slider_width.set(640); self.slider_width.grid(row=1, column=1, padx=5, sticky="we")
        
        # Höhe
        tk.Label(slider_frame, text="Height:", fg="white", bg="#1e1e1e", font=("Arial", 10, "bold")).grid(row=0, column=2, sticky="w")
        self.slider_height = Scale(slider_frame, from_=256, to=768, orient="horizontal", resolution=64, bg="#1e1e1e", fg="white", highlightthickness=0); self.slider_height.set(512); self.slider_height.grid(row=1, column=2, padx=5, sticky="we")
        
        # Frames (Video)
        tk.Label(slider_frame, text="Video frames:", fg="white", bg="#1e1e1e", font=("Arial", 10, "bold")).grid(row=0, column=3, sticky="w")
        self.slider_frames = Scale(slider_frame, from_=4, to=32, orient="horizontal", bg="#1e1e1e", fg="white", highlightthickness=0); self.slider_frames.set(14); self.slider_frames.grid(row=1, column=3, padx=5, sticky="we")
        
        # Strength (Img2Img)
        tk.Label(slider_frame, text="Img2Img Strength:", fg="white", bg="#1e1e1e", font=("Arial", 10, "bold")).grid(row=0, column=4, sticky="w")
        self.slider_strength = Scale(slider_frame, from_=0.1, to=1.0, orient="horizontal", resolution=0.05, bg="#1e1e1e", fg="white", highlightthickness=0); self.slider_strength.set(0.75); self.slider_strength.grid(row=1, column=4, padx=5, sticky="we")
        
        # Anzahl Bilder/Videos (Batch Count)
        tk.Label(slider_frame, text="Number:", fg="#2ecc71", bg="#1e1e1e", font=("Arial", 10, "bold")).grid(row=0, column=5, sticky="w")
        self.slider_batch = Scale(slider_frame, from_=1, to=100, orient="horizontal", bg="#1e1e1e", fg="#2ecc71", highlightthickness=0); self.slider_batch.set(1); self.slider_batch.grid(row=1, column=5, padx=5, sticky="we")

        slider_frame.columnconfigure((0,1,2,3,4,5), weight=1)

        # Buttons
        btn_frame = tk.Frame(self.root, bg="#1e1e1e")
        btn_frame.pack(fill="x", padx=20, pady=20)

        self.btn_img = tk.Button(btn_frame, text="GENERATE IMAGE", bg="#2ecc71", fg="white", font=("Arial", 11, "bold"), command=self.start_gen_img)
        self.btn_img.pack(side="left", expand=True, fill="x", padx=5)

        self.btn_gif = tk.Button(btn_frame, text="GIF GENERIEREN", bg="#e91e63", fg="white", font=("Arial", 11, "bold"), command=lambda: self.start_gen_vid("gif"))
        self.btn_gif.pack(side="left", expand=True, fill="x", padx=5)

        self.btn_mp4 = tk.Button(btn_frame, text="GENERATE MP4", bg="#3498db", fg="white", font=("Arial", 11, "bold"), command=lambda: self.start_gen_vid("mp4"))
        self.btn_mp4.pack(side="left", expand=True, fill="x", padx=5)

        # Status
        self.status_label = tk.Label(self.root, text="Ready. (Tip: Steps < 10 automatically activate Turbo-LCM)", fg="#2ecc71", bg="#1e1e1e", font=("Arial", 10))
        self.status_label.pack(pady=10)

        # --- DER NEUE LUXUS-BUTTON ---
        self.btn_mp4_audio = tk.Button(self.root, text="GENERATE MP4 + AUDIO", bg="purple", fg="white", font=("Arial", 11, "bold"), command=self.start_video_mit_audio)
        self.btn_mp4_audio.pack(fill="x", padx=20, pady=5)

    # --- HELPER UI ---
    def add_file_selector(self, txt, var, cmd):
        tk.Label(self.root, text=txt, fg="white", bg="#1e1e1e", font=("Arial", 9, "bold")).pack(anchor="w", padx=20, pady=(10, 0))
        f = tk.Frame(self.root, bg="#1e1e1e"); f.pack(fill="x", padx=20)
        tk.Entry(f, textvariable=var, bg="#2d2d2d", fg="white", insertbackground="white", borderwidth=0).pack(side="left", expand=True, fill="x", ipady=4, padx=(0, 5))
        tk.Button(f, text="Browse", command=cmd, bg="#444", fg="white", relief="flat", padx=10).pack(side="left")

    def add_text_entry(self, txt, var, p):
        tk.Label(self.root, text=txt, fg="white", bg="#1e1e1e", font=("Arial", 9, "bold")).pack(anchor="w", padx=20, pady=(p, 0))
        tk.Entry(self.root, textvariable=var, bg="#2d2d2d", fg="white", insertbackground="white", borderwidth=0).pack(fill="x", padx=20, ipady=4)

    # --- BROWSE FUNKTIONEN ---
    def browse_model(self): f = filedialog.askopenfilename(); self.model_path.set(f) if f else None
    def browse_lora(self): f = filedialog.askopenfilename(); self.lora_path.set(f) if f else None
    def browse_input_image(self): f = filedialog.askopenfilename(); self.input_image_path.set(f) if f else None
    def browse_text_encoder(self): f = filedialog.askopenfilename(); self.text_encoder_path.set(os.path.dirname(f)) if f else None
    def browse_motion_adapter(self): f = filedialog.askopenfilename(); self.motion_adapter_path.set(os.path.dirname(f)) if f else None

    # --- LOGIK ---
    def set_busy(self, is_video=False):
        for b in [self.btn_img, self.btn_gif, self.btn_mp4]: b.config(state="disabled")
        t = "Video Turbo is being calculated..." if self.slider_steps.get() < 10 and is_video else "Generation starts..."
        self.status_label.config(text=t, fg="orange")

    def reset_status(self, msg):
        for b in [self.btn_img, self.btn_gif, self.btn_mp4]: b.config(state="normal")
        self.status_label.config(text=msg, fg="#2ecc71")

    def apply_turbo_logic(self, pipe):
        if self.slider_steps.get() < 10:
            print("LCM turbo detected! Switch scheduler...")
            pipe.scheduler = LCMScheduler.from_config(pipe.scheduler.config)
            return 1.0 
        return 5 

    # --- GENERIERUNG BILD ---
    def start_gen_img(self):
        self.set_busy()
        threading.Thread(target=self.gen_img_thread, daemon=True).start()

    # --- GENERIERUNG BILD (Der funktionierende Original-Zustand + VAE Brücke) ---
    def gen_img_thread(self):
        self.cleanup() 
        try:
            start_t = time.time()
            te_p = self.text_encoder_path.get() or "runwayml/stable-diffusion-v1-5"
            self.status_label.config(text="Download clean image tuning...", fg="orange")
            
            # ALLES IN 16-BIT (Verhindert Half != float Clashes!)
            self.text_encoder = CLIPTextModel.from_pretrained(te_p, torch_dtype=torch.float16, low_cpu_mem_usage=False, **({"subfolder":"text_encoder"} if "runwayml" in te_p else {}))

            p_cls = StableDiffusionImg2ImgPipeline if self.input_image_path.get() else StableDiffusionPipeline
            self.pipe = p_cls.from_single_file(
                self.model_path.get(), 
                text_encoder=self.text_encoder, 
                torch_dtype=torch.float16 
            )
            
            # VAE auf 32-Bit (Der bewährte Fix für die 1060)
            self.pipe.vae.to(dtype=torch.float32)

            # DIE MAGISCHE BRÜCKE (Matsch-Killer)
            original_decode = self.pipe.vae.decode
            def safe_decode(z, *args, **kwargs):
                return original_decode(z.to(dtype=torch.float32), *args, **kwargs)
            self.pipe.vae.decode = safe_decode

            if self.lora_path.get():
                self.pipe.load_lora_weights(self.lora_path.get())
                self.pipe.fuse_lora(lora_scale=0.4)

            guidance = self.apply_turbo_logic(self.pipe)
            
            # VRAM-Management
            self.pipe.enable_model_cpu_offload() 
            self.pipe.vae.enable_slicing()
            # self.pipe.vae.enable_tiling()
            
            batch_count = self.slider_batch.get()
            try:
                base_seed = int(self.seed_var.get())
                # Wenn der Regler auf -1 steht (oder eingetippt wurde), 
                # zwingen wir ihn hart auf die 1!
                if base_seed == -1:
                    base_seed = 1
            except ValueError:
                # Wenn das Feld leer ist oder Buchstaben drinstehen -> auch Seed 1
                base_seed = 1 

            for i in range(batch_count):
                # Er rechnet jetzt einfach: 1 + 0 = 1, 1 + 1 = 2, usw.
                current_seed = base_seed + i 
                
                device = "cuda" if torch.cuda.is_available() else "cpu"
                generator = torch.Generator(device=device).manual_seed(current_seed)

                self.start_rennauto(f"Bild {i+1} von {batch_count} wird gerendert... (Seed: {current_seed})")
                
                args = {"prompt": self.prompt.get(), "negative_prompt": self.neg_prompt.get(), 
                        "num_inference_steps": self.slider_steps.get(), "guidance_scale": guidance,
                        "generator": generator} 
                
                if self.input_image_path.get():
                    init_img = PILImage.open(self.input_image_path.get()).convert("RGB").resize((self.slider_width.get(), self.slider_height.get()))
                    args.update({"image": init_img, "strength": self.slider_strength.get()})
                else:
                    args.update({"width": self.slider_width.get(), "height": self.slider_height.get()})

                output = self.pipe(**args).images[0]
                
                fname = f"out_img_{int(time.time())}_seed{current_seed}.png"
                output.save(fname)

            self.stop_rennauto(f"Goal achieved! {batch_count} Image(s) saved in {time.time()-start_t:.1f}s.")
            self.cleanup() 
            
        except Exception as e:
            print(f"Abbruch: {e}")
            self.cleanup() 
            if hasattr(self, 'stop_rennauto'):
                self.stop_rennauto("Fehler!", "red")
    
    def start_video_mit_audio(self):
        # Merken: Wir wollen Audio!
        self.mit_audio_mixen = True 
        self.start_gen_vid("mp4") # <--- Hier war der Namens-Fehler!
                
    # --- START GENERIERUNG VIDEO ---
    def start_gen_vid(self, fmt):
        self.mit_audio_mixen = False # WICHTIG: Resettet den Schalter bei normalem Klick!
        self.set_busy(True)
        threading.Thread(target=self.gen_vid_thread, args=(fmt,), daemon=True).start()

    # --- GENERIERUNG VIDEO (Pure Stabilität ohne verrückte Hacks) ---
    def gen_vid_thread(self, fmt):
        self.cleanup()
        try:
            start_t = time.time()
            ma_p = self.motion_adapter_path.get()
            if not ma_p or ma_p.strip() == "":
                ma_p = "guoyww/animatediff-motion-adapter-v1-5-2"
                print("Ziehe Motion-Adapter frisch aus dem Netz...")
                
            te_p = self.text_encoder_path.get()
            if not te_p or te_p.strip() == "":
                te_p = "runwayml/stable-diffusion-v1-5"
                print("Ziehe Text-Encoder frisch aus dem Netz...")
            
            if "PYTORCH_CUDA_ALLOC_CONF" in os.environ:
                del os.environ["PYTORCH_CUDA_ALLOC_CONF"]

            self.status_label.config(text="Lade Video-Modelle (Clean-Track)...", fg="orange")
            
            # ALLES IN 16-BIT LADEN
            self.adapter = MotionAdapter.from_pretrained(ma_p, torch_dtype=torch.float16)
            self.text_encoder = CLIPTextModel.from_pretrained(
                te_p, 
                torch_dtype=torch.float16, 
                **({"subfolder": "text_encoder"} if "runwayml" in te_p else {})
            )
            self.pipe = AnimateDiffPipeline.from_single_file(
                self.model_path.get(), 
                motion_adapter=self.adapter, 
                text_encoder=self.text_encoder, 
                torch_dtype=torch.float16
            )
            
            # VAE auf 32-Bit zwingen
            self.pipe.vae.to(dtype=torch.float32)

            # DIE MAGISCHE BRÜCKE FÜR DEN MALER
            original_decode = self.pipe.vae.decode
            def safe_decode(z, *args, **kwargs):
                return original_decode(z.to(dtype=torch.float32), *args, **kwargs)
            self.pipe.vae.decode = safe_decode

            guidance = self.apply_turbo_logic(self.pipe)
            
            # GETRIEBE-SETUP
            self.pipe.enable_model_cpu_offload() 
            self.pipe.vae.enable_slicing()
            self.pipe.enable_attention_slicing(1)

            batch_count = self.slider_batch.get()
            
            # 1. Den Start-Seed sicher aus der GUI holen
            try:
                start_val = int(self.seed_var.get())
                if start_val == -1: 
                    start_val = 1 # Aus Zufall (-1) wird stur die 1
            except ValueError:
                start_val = 1 # Falls das Feld leer ist
                
            # 2. Die Video-Schleife
            for i in range(batch_count):
                # Wir nennen es zwingend "seed", damit dein Dateiname unten funktioniert!
                seed = start_val + i 
                generator = torch.Generator(device="cuda").manual_seed(seed)

                self.start_rennauto(f"Video {i+1}/{batch_count} wird gerendert...")

                output = self.pipe(
                    prompt=self.prompt.get(),
                    negative_prompt=self.neg_prompt.get(),
                    num_frames=self.slider_frames.get(),
                    num_inference_steps=self.slider_steps.get(),
                    guidance_scale=guidance,
                    width=self.slider_width.get(),
                    height=self.slider_height.get(),
                    generator=generator
                )

                fname = f"out_vid_{int(time.time())}_seed{seed}.{fmt}"
                if fmt == "mp4": export_to_video(output.frames[0], fname, fps=8)
                else: export_to_gif(output.frames[0], fname)

            self.cleanup()
            self.stop_rennauto(f"Goal achieved! Video(s) saved in {time.time()-start_t:.1f}s.")
            
            # --- DIE AUDIO-WEICHE (NUR NOCH MUXER) ---
            if getattr(self, 'mit_audio_mixen', False):
                
                # --- PHASE 1: VRAM LEEREN ---
                print("Cleared the VRAM after the video...")
                if hasattr(self, 'pipe'): del self.pipe 
                torch.cuda.empty_cache() 
                gc.collect()
                
                # --- PHASE 2: FFMPEG MUXING ---
                # <--- import subprocess und import os weggelassen!
                
                final_music_video = f"MIX_vid_{int(time.time())}_seed{seed}.mp4"
                video_pfad = fname 
                audio_pfad = "final_mix.wav" 
                
                if os.path.exists(audio_pfad):
                    print(f"Welds {video_pfad} and {audio_pfad} together...")
                    cmd = ["ffmpeg", "-y", "-i", video_pfad, "-i", audio_pfad, "-c:v", "copy", "-c:a", "aac", "-map", "0:v:0", "-map", "1:a:0", "-shortest", final_music_video]
                    subprocess.run(cmd)
                    print(f"BOOM! Music video finished: {final_music_video}")
                else:
                    print(f"ERROR: '{audio_pfad}' Not found!")
                    print("Did you forget to mix the beat in the C++ tool?")
            else:
                print("Silent video saved. (No audio requested)")

        except Exception as e:
            print(f"Abbruch: {e}")
            import traceback
            traceback.print_exc() 
            self.cleanup()
            if hasattr(self, 'stop_rennauto'):
                self.stop_rennauto("Error!", "red")

# --- START ---
if __name__ == "__main__":
    root = tk.Tk()
    app = ImageGenGUI(root)
    root.mainloop()