package com.tassadar.fbstream;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStreamWriter;

import android.app.Activity;
import android.content.Context;
import android.content.SharedPreferences;
import android.content.SharedPreferences.Editor;
import android.content.res.AssetManager;
import android.os.Bundle;
import android.util.Log;
import android.view.Menu;
import android.view.MenuItem;
import android.view.View;
import android.view.Window;
import android.widget.Button;
import android.widget.EditText;
import android.widget.RadioButton;
import android.widget.Toast;


public class MainActivity extends Activity {

    @Override
    public void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        requestWindowFeature(Window.FEATURE_INDETERMINATE_PROGRESS);
        setContentView(R.layout.activity_main);

        m_binary = (EditText)findViewById(R.id.binary);
        m_ip = (EditText)findViewById(R.id.ip);
        m_port = (EditText)findViewById(R.id.port);
        m_quality = (EditText)findViewById(R.id.quality);
        m_scale = (EditText)findViewById(R.id.scale);
        m_btn = (Button)findViewById(R.id.start);
        m_tcp = (RadioButton)findViewById(R.id.tcp);
        m_udp = (RadioButton)findViewById(R.id.udp);
        m_32bit = (RadioButton)findViewById(R.id.bit32);
        m_16bit = (RadioButton)findViewById(R.id.bit16);

        SharedPreferences cfg = getPreferences(MODE_PRIVATE);
        m_binary.setText(cfg.getString("binPath", ""));
        m_ip.setText(cfg.getString("ip", "192.168.0.154"));
        m_port.setText(cfg.getString("port", "33334"));
        m_quality.setText(cfg.getString("quality", "60"));
        m_scale.setText(cfg.getString("scale", "100"));
        m_last_copy = cfg.getLong("lastCopy", 0);
        m_udp.setChecked(cfg.getBoolean("useUDP", true));
        m_tcp.setChecked(!cfg.getBoolean("useUDP", true));
        m_32bit.setChecked(!cfg.getBoolean("16bit", false));
        m_16bit.setChecked(cfg.getBoolean("16bit", false));

        enableInput(false);
        
        checkRunning();
    }

    @Override
    public boolean onCreateOptionsMenu(Menu menu) {
        getMenuInflater().inflate(R.menu.activity_main, menu);
        return true;
    }

    @Override
    public boolean onOptionsItemSelected(MenuItem item) {
        switch (item.getItemId()) {
            case R.id.menu_check:
                checkRunning();
                return true;
            default:
                return super.onOptionsItemSelected(item);
        }
    }

    @Override
    protected void onPause() {
        super.onPause();
        saveCfg();
    }

    private void saveCfg()
    {
        Editor cfg = getPreferences(MODE_PRIVATE).edit();
        cfg.putString("binPath", m_binary.getText().toString());
        cfg.putString("ip", m_ip.getText().toString());
        cfg.putString("port", m_port.getText().toString());
        cfg.putString("quality", m_quality.getText().toString());
        cfg.putString("scale", m_scale.getText().toString());
        cfg.putLong("lastCopy", m_last_copy);
        cfg.putBoolean("useUDP", m_udp.isChecked());
        cfg.putBoolean("16bit", m_16bit.isChecked());
        cfg.commit();
    }

    private void enableInput(boolean enable)
    {
        m_binary.setEnabled(enable);
        m_ip.setEnabled(enable);
        m_port.setEnabled(enable);
        m_quality.setEnabled(enable);
        m_scale.setEnabled(enable);
        m_udp.setEnabled(enable);
        m_tcp.setEnabled(enable);
        m_32bit.setEnabled(enable);
        m_16bit.setEnabled(enable);
        m_btn.setText(enable ? "Start" : "Stop");
    }

    private void checkRunning()
    {
        m_btn.setEnabled(false);
        setProgressBarIndeterminateVisibility(true);
        new Thread(new Runnable(){
           public void run()
           {
               final String pidof = MainActivity.runCmd("pidof fbstream");
               m_running = pidof.length() != 0;
               m_binary.post(new Runnable() {
                   public void run()
                   {
                       enableInput(!m_running);
                       setProgressBarIndeterminateVisibility(false);
                       m_btn.setEnabled(true);
                   }
               });
           }
        }).start();
    }

    private String copyBin() {
        String bin = "";
        try {
            bin = getFilesDir().toString() + "/fbstream";

            byte[] buff = new byte[50];
            File f = new File(bin);
            if(f.exists())
            {
                InputStream date = getAssets().open("copytime.txt", AssetManager.ACCESS_BUFFER);
                int res = date.read(buff);
                date.close();

                if(res != -1)
                {
                    String str = new String(buff).trim();
                    long val = Long.decode(str);

                    if(val <= m_last_copy) // do not copy, older file
                        return bin;
                    m_last_copy = val;
                }
            }
            f = null;

            InputStream in = getAssets().open("fbstream");
            FileOutputStream out = openFileOutput("fbstream", Context.MODE_PRIVATE);

            buff = new byte[5000];
            for(int res = in.read(buff); res != -1; res = in.read(buff))
                out.write(buff);

            out.close();
            in.close();

            MainActivity.runCmd("chmod 775 " + bin);
        } catch (IOException e) {
            m_btn.post(new Runnable() {
                public void run()
                {
                    enableInput(!m_running);
                    m_btn.setEnabled(true);
                    setProgressBarIndeterminateVisibility(false);

                    Toast toast = Toast.makeText(getApplicationContext(),
                            "Failed to copy fbstream binary!", Toast.LENGTH_SHORT);
                    toast.show();
                }
            });
            return "";
        }
        return bin;
    }

    public void start_onClick(View v) {
        m_btn.setEnabled(false);
        saveCfg();

        setProgressBarIndeterminateVisibility(true);

        new Thread(new Runnable(){
            public void run()
            {
                String pidof = MainActivity.runCmd("pidof fbstream");

                if(m_running != (pidof.length() != 0)) {
                    m_running = pidof.length() != 0;
                    m_binary.post(new Runnable() {
                        public void run()
                        {
                            enableInput(!m_running);
                            m_btn.setEnabled(true);
                            setProgressBarIndeterminateVisibility(false);
                        }
                    });
                    return;
                }

                String str = "";
                if(!m_running) {
                    String bin = m_binary.getText().toString();
                    if(bin.length() != 0 && !bin.endsWith("/fbstream"))
                        bin += "/fbstream";

                    if (bin.length() == 0)
                        bin = copyBin();

                    str += "nohup ";
                    str += bin + " ";
                    str += m_ip.getText() + " ";
                    str += m_port.getText() + " ";
                    str += m_udp.isChecked() ? "udp " : "tcp ";
                    str += m_quality.getText() + " ";
                    str += m_scale.getText() + " ";
                    if(m_16bit.isChecked())
                        str += "16bit ";
                    str += " > /dev/null 2>&1 &";
                }
                else
                {
                    str += "kill -9 $(pidof fbstream)";
                }

                MainActivity.runCmd(str);

                pidof = MainActivity.runCmd("pidof fbstream");
                m_running = pidof.length() != 0;
                m_binary.post(new Runnable() {
                    public void run()
                    {
                        enableInput(!m_running);
                        m_btn.setEnabled(true);
                        setProgressBarIndeterminateVisibility(false);
                    }
                });
            }
         }).start();
    }

    private static final String runCmd(String command) {
        Process proc = null;
        OutputStreamWriter osw = null;
        StringBuilder sbstdOut = new StringBuilder();

        Log.i("fbstream", command);

        try {
            proc = Runtime.getRuntime().exec("su");
            osw = new OutputStreamWriter(proc.getOutputStream());
            osw.write(command);
            osw.flush();
            osw.close();
        } catch (IOException ex) {
            ex.printStackTrace();
            return null;
        } finally {
            if (osw != null) {
                try {
                    osw.close();
                } catch (IOException e) {
                    e.printStackTrace();
                    return null;
                }
            }
        }
        try {
            if (proc != null)
                proc.waitFor();
        } catch (InterruptedException e) {
            e.printStackTrace();
            return null;
        }

        int read;
        try {
            while((read = proc.getInputStream().read()) != -1)
                sbstdOut.append((char)read);
            while((read = proc.getErrorStream().read()) != -1)
                sbstdOut.append((char)read);
        } catch (IOException e) { }
        
        String res = sbstdOut.toString();

        // What the hell?!
        res = res.replace("FIX ME! implement ttyname() bionic/libc/bionic/stubs.c:360", "");

        if(res.equals("\n") || res.split("\n").length == 1)
            res = res.replaceAll("\n", "");
        proc.destroy();
        return res;
    }

    long m_last_copy;
    boolean m_running;
    EditText m_binary;
    EditText m_ip;
    EditText m_port;
    EditText m_quality;
    EditText m_scale;
    Button m_btn;
    RadioButton m_tcp;
    RadioButton m_udp;
    RadioButton m_32bit;
    RadioButton m_16bit;
}
