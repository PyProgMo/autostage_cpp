import matplotlib.pyplot as plt
import os

class specploter():
    def __init__(self):
        self.fig, self.ax = plt.subplots()
        self.ax.set_xlabel('Wavelength (nm)')
        self.ax.set_ylabel('Intensity (a.u.)')
        self.ax.set_title('Spectral Data')
        self.ax.grid(True)
    
    def clear_plot(self):
        self.ax.cla()
        self.ax.set_xlabel('Wavelength (nm)')
        self.ax.set_ylabel('Intensity (a.u.)')
        self.ax.set_title('Spectral Data')
        self.ax.grid(True)
    
    def loadspec(self, filepath):
        """
        load and plot spectra from captures folder of this project, data format:
        amera_index=0
        exposure_s=0.1
        status=20073 (DRV_IDLE)
        pixel,count
        0,2132
        1,2019
        2,2003
        3,1986
        4,1998
        5,2018
        6,2001
        """
        metadata = {}
        # load data into metadata as long as "=" exists as delimiter, then get rownames with ","
        loadstate = 0 # 0, metadata, 1 data
        filename = os.path.splitext(os.path.basename(filepath))[0]
        metadata['filename'] = filename
        wl = []
        spec = []
        with open(filepath, 'r') as f:
            for line in f:
                if loadstate == 0:
                    if '=' in line:
                        key, value = line.strip().split('=', 1)
                        metadata[key] = value
                    elif ',' in line:
                        rownames = line.strip().split(',')
                        loadstate = 1
                elif loadstate == 1:
                    # parse data rows
                    data = line.strip().split(',')
                    if len(data) == 2:
                        wl.append(float(data[0]))
                        spec.append(int(data[1]))

        return wl, spec, metadata

    def plot(self, wl, spec, metadata, show=False, save=True, savedir='plots'):
        self.ax.plot(wl, spec, label='Spectra')
        self.ax.legend()
        self.ax.set_title(f"Spectral Data - Camera Index: {metadata.get('amera_index', 'N/A')}, Exposure: {metadata.get('exposure_s', 'N/A')}s")
        if save:
            output_file = os.path.join(savedir, f"{metadata['filename']}_spectra.png")
            plt.savefig(output_file)
        if show:
            plt.show()
        else: 
            self.clear_plot()
    
    def plot_and_save_all(self, specpath=None, show=False, savedir='plot'):
        if specpath is None:
            specpath = os.path.join(os.getcwd(), 'captures')
        # ensure the output directory exists inside the captures folder
        plotdir = os.path.join(specpath, savedir)
        os.makedirs(plotdir, exist_ok=True)
        
        for filename in os.listdir(specpath):
            if filename.endswith('.txt'):
                filepath = os.path.join(specpath, filename)
                wl, spec, metadata = self.loadspec(filepath)
                self.plot(wl, spec, metadata, show=show, save=True, savedir=plotdir)

if __name__ == "__main__":
    sp = specploter()
    sp.plot_and_save_all(show=False, savedir='plot')